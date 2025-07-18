// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <list>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

// Pull in the modified Imath headers and the OSL_HOSTDEVICE macro
#ifdef __CUDACC__
#    include <OSL/oslconfig.h>
#endif

#ifdef __CUDACC__
#    include <OSL/hashes.h>
#endif

#include <OpenImageIO/Imath.h>

#include <OpenImageIO/color.h>
#include <OpenImageIO/paramlist.h>
#include <OpenImageIO/refcnt.h>
#include <OpenImageIO/texture.h>
#include <OpenImageIO/thread.h>
#include <OpenImageIO/ustring.h>

#include "osl_pvt.h"

#include <OSL/device_ptr.h>
#include <OSL/dual.h>
#include <OSL/dual_vec.h>
#include <OSL/genclosure.h>
#include <OSL/llvm_util.h>
#include <OSL/mask.h>
#include <OSL/oslclosure.h>
#include <OSL/oslexec.h>
#include <OSL/rendererservices.h>
#include <OSL/shaderglobals.h>

#include "shading_state_uniform.h"
#include "constantpool.h"
#include "opcolor.h"


using namespace OSL;
using namespace OSL::pvt;

using OIIO::atomic_int;
using OIIO::atomic_ll;
using OIIO::lock_guard;
using OIIO::mutex;
using OIIO::ParamValueList;
using OIIO::RefCnt;
using OIIO::spin_lock;
using OIIO::spin_mutex;
namespace Strutil = OIIO::Strutil;


OSL_NAMESPACE_BEGIN



struct PerThreadInfo {
    PerThreadInfo();
    ~PerThreadInfo();
    ShadingContext* pop_context();  ///< Get the pool top and then pop

    std::stack<ShadingContext*> context_pool;
    LLVM_Util::PerThreadInfo llvm_thread_info;
};



namespace pvt {

void
optix_cache_unwrap(string_view cache_value, std::string& ptx,
                   size_t& groupdata_size);
std::string
optix_cache_wrap(string_view ptx, size_t groupdata_size);

// forward definitions
class ShadingSystemImpl;
class ShaderInstance;
typedef std::shared_ptr<ShaderInstance> ShaderInstanceRef;
class Dictionary;
class RuntimeOptimizer;
class BackendLLVM;
#if OSL_USE_BATCHED
class BatchedBackendLLVM;
#endif
struct ConnectedParam;

OSL_DLL_EXPORT void
print_closure(std::ostream& out, const ClosureColor* closure,
              ShadingSystemImpl* ss, bool treat_ustrings_as_hash);

/// Signature of the function that LLVM generates to run the shader
/// group.
typedef void (*RunLLVMGroupFunc)(void* shaderglobals, void* heap_arena_ptr,
                                 void* userdata_base_pointer,
                                 void* output_base_pointer, int shadeindex,
                                 void* interactive_params_ptr);
#if OSL_USE_BATCHED
typedef void (*RunLLVMGroupFuncWide)(void* batchedshaderglobals,
                                     void* heap_arena_ptr,
                                     const void* wide_shade_index,
                                     void* userdata_base_pointer,
                                     void* output_base_pointer,
                                     int run_mask_value,
                                     void* interactive_params_ptr);
#endif

/// Signature of a constant-folding method
typedef int (*OpFolder)(RuntimeOptimizer& rop, int opnum);

/// Signature of an LLVM-IR-generating method
typedef bool (*OpLLVMGen)(BackendLLVM& rop, int opnum);
#if OSL_USE_BATCHED
typedef bool (*OpLLVMGenWide)(BatchedBackendLLVM& rop, int opnum);
#endif

struct OpDescriptor {
    ustring name;       // name of op
    OpLLVMGen llvmgen;  // llvm-generating routine
#if OSL_USE_BATCHED
    OpLLVMGenWide llvmgenwide;  // wide version of llvm-generating routine
#endif
    OpFolder folder;     // constant-folding routine
    bool simple_assign;  // wholly overwrites arg0, no other writes,
                         //     no side effects
    int flags;           // other flags
    OpDescriptor() {}
    OpDescriptor(const char* n, OpLLVMGen ll,
#if OSL_USE_BATCHED
                 OpLLVMGenWide llw,
#endif
                 OpFolder fold = NULL, bool simple = false, int flags = 0)
        : name(n)
        , llvmgen(ll)
        ,
#if OSL_USE_BATCHED
        llvmgenwide(llw)
        ,
#endif
        folder(fold)
        , simple_assign(simple)
        , flags(flags)
    {
    }

    enum FlagValues {
        None        = 0,
        SideEffects = 1,
        Tex         = 2,
        StrCreate   = 4,   // creates strings
        StrChars    = 8,   // accesses string characters
        PoliceMisc  = 16,  // misc opcodes to be policed
        Police      = Tex | StrCreate | StrChars | PoliceMisc
    };
};



// Helper function to expand vec by 'size' elements, initializing them to 0.
template<class T>
inline void
expand(std::vector<T>& vec, size_t size)
{
    vec.resize(vec.size() + size, T(0));
}



// Struct to hold records about what user data a group needs
struct UserDataNeeded {
    ustring name;
    int layer_num;
    TypeDesc type;
    void* data;
    bool derivs;

    UserDataNeeded(ustring name, int layer_num, TypeDesc type,
                   void* data = NULL, bool derivs = false)
        : name(name)
        , layer_num(layer_num)
        , type(type)
        , data(data)
        , derivs(derivs)
    {
    }
    friend bool operator<(const UserDataNeeded& a, const UserDataNeeded& b)
    {
        if (a.name != b.name)
            return a.name < b.name;
        // Checking for layer_num means that if derivs differ find_userdata_index
        // may find the wrong layer symbol with the wrong derivs setting.
        //if (a.layer_num != b.layer_num)
        //    return a.layer_num < b.layer_num;
        if (a.type.basetype != b.type.basetype)
            return a.type.basetype < b.type.basetype;
        if (a.type.aggregate != b.type.aggregate)
            return a.type.aggregate < b.type.aggregate;
        if (a.type.arraylen != b.type.arraylen)
            return a.type.arraylen < b.type.arraylen;
        // Ignore vector semantics
        // if (a.type.vecsemantics != b.type.vecsemantics)
        //     return a.type.vecsemantics < b.type.vecsemantics;
        // Do not sort based on derivs
        return false;  // they are equal
    }
};

// Struct defining an attribute needed by a shader group
struct AttributeNeeded {
    ustring name;
    ustring scope;
    TypeDesc type;
    bool derivs;

    AttributeNeeded(ustring name, ustring scope = ustring(),
                    TypeDesc type = TypeUnknown, bool derivs = false)
        : name(name), scope(scope), type(type), derivs(derivs)
    {
    }

    friend bool operator<(const AttributeNeeded& a, const AttributeNeeded& b)
    {
        if (a.name != b.name)
            return a.name < b.name;
        if (a.scope != b.scope)
            return a.scope < b.scope;
        if (a.type.basetype != b.type.basetype)
            return a.type.basetype < b.type.basetype;
        if (a.type.aggregate != b.type.aggregate)
            return a.type.aggregate < b.type.aggregate;
        if (a.type.arraylen != b.type.arraylen)
            return a.type.arraylen < b.type.arraylen;
        // Ignore vector semantics
        // if (a.type.vecsemantics != b.type.vecsemantics)
        //     return a.type.vecsemantics < b.type.vecsemantics;
        // Do not sort based on derivs
        return false;  // they are equal
    }
};

// Prefix for OSL shade op declarations. Make them local visibility, but
// "C" linkage (no C++ name mangling).
#define OSL_SHADEOP extern "C" OSL_DLL_LOCAL


// Handy re-casting macros
inline ustring
USTR(ustring_pod s) noexcept
{
    return OSL::bitcast<ustring>(s);
}


#define MAT(m)      (*(Matrix44*)m)
#define VEC(v)      (*(Vec3*)v)
#define DFLOAT(x)   (*(Dual2<Float>*)x)
#define DVEC(x)     (*(Dual2<Vec3>*)x)
#define COL(x)      (*(Color3*)x)
#define DCOL(x)     (*(Dual2<Color3>*)x)
#define TYPEDESC(x) OSL::bitcast<TypeDesc, long long>(x)

/// Like an int (of type T), but also internally keeps track of the
/// maximum value is has held, and the total "requested" deltas.
/// You really shouldn't use an unsigned type for T, for two reasons:
/// (1) Our implementation of '-=' will fail; and (2) you actually
/// want to allow the counter to go negative, to detect if you have
/// made a mistake in your bookkeeping by forgetting an allocation.
template<typename T> class PeakCounter {
public:
    typedef T value_t;
    PeakCounter() : m_current(0), m_requested(0), m_peak(0) {}
    /// Reset all counts to zero.
    ///
    void clear()
    {
        m_current   = 0;
        m_requested = 0;
        m_peak      = 0;
    }
    /// Return the current value.
    ///
    value_t operator()() const { return m_current; }

    /// Return the current value.
    ///
    value_t current(void) const { return m_current; }
    /// Return the sum of all requests.
    ///
    value_t requested(void) const { return m_requested; }
    /// Return the peak value we saw.
    ///
    value_t peak(void) const { return m_peak; }

    /// Reassign the current value, adjust peak and requested as necessary.
    ///
    const value_t operator=(value_t newval)
    {
        value_t cur = m_current;
        if (newval > cur)
            m_requested += (cur - newval);
        m_current = newval;
        if (newval > m_peak)
            m_peak = newval;
        return m_current;
    }
    /// Add to current value, adjust peak and requested as necessary.
    ///
    const value_t operator+=(value_t sz)
    {
        m_current += sz;
        if (sz > 0) {
            m_requested += sz;
            if (m_current > m_peak)
                m_peak = m_current;
        }
        return m_current;
    }
    /// Subtract from current value
    ///
    const value_t operator-=(value_t sz)
    {
        *this += (-sz);
        return m_current;
    }

    const value_t operator++()
    {
        *this += 1;
        return m_current;
    }
    const value_t operator++(int)
    {
        *this += 1;
        return m_current - 1;
    }
    const value_t operator--()
    {
        *this -= 1;
        return m_current;
    }
    const value_t operator--(int)
    {
        *this -= 1;
        return m_current + 1;
    }

    friend std::ostream& operator<<(std::ostream& out, const PeakCounter& p)
    {
        out << p.requested() << " requested, " << p.peak() << " peak, "
            << p.current() << " current";
        return out;
    }

    std::string memstat() const
    {
        return Strutil::memformat(requested()) + " requested, "
               + Strutil::memformat(peak()) + " peak, "
               + Strutil::memformat(current()) + " current";
    }

private:
    value_t m_current, m_requested, m_peak;
};



/// Template to count a vector's allocated size, in bytes.
///
template<class T>
inline off_t
vectorbytes(const std::vector<T>& v)
{
    return v.capacity() * sizeof(T);
}



/// ShaderMaster is the full internal representation of a complete
/// shader that would be a .oso file on disk: symbols, instructions,
/// arguments, you name it.  A master copy is shared by all the
/// individual instances of the shader.
class ShaderMaster final : public RefCnt {
public:
    typedef OIIO::intrusive_ptr<ShaderMaster> ref;
    ShaderMaster(ShadingSystemImpl& shadingsys);
    ~ShaderMaster();

    std::string print();  // Debugging

    /// Return a pointer to the shading system for this master.
    ///
    ShadingSystemImpl& shadingsys() const { return m_shadingsys; }

    /// Run through the symbols and set up various things we can know
    /// with just the master: the size (including padding), and their
    /// data pointers if they are constants or params (to the defaults).
    /// As a side effect, also set this->m_firstparam/m_lastparam.
    void resolve_syms();

    /// Find the named symbol, return its index in the symbol array, or
    /// -1 if not found.
    int findsymbol(ustring name) const;

    /// Return a pointer to the symbol (specified by integer index),
    /// or NULL (if index was -1, as returned by 'findsymbol').
    Symbol* symbol(int index)
    {
        OSL_DASSERT(index < (int)m_symbols.size());
        return index >= 0 ? &m_symbols[index] : NULL;
    }
    const Symbol* symbol(int index) const
    {
        OSL_DASSERT(index < (int)m_symbols.size());
        return index >= 0 ? &m_symbols[index] : NULL;
    }

    /// Return the name of the shader.
    ///
    const std::string& shadername() const { return m_shadername; }

    const std::string& osofilename() const { return m_osofilename; }

    ShaderType shadertype() const { return m_shadertype; }
    string_view shadertypename() const
    {
        return OSL::pvt::shadertypename(m_shadertype);
    }

    /// Where is the location that holds the parameter's default value?
    void* param_default_storage(int index);
    const void* param_default_storage(int index) const;

    int num_params() const { return m_lastparam - m_firstparam; }

    int raytype_queries() const { return m_raytype_queries; }

    bool range_checking() const { return m_range_checking; }
    void range_checking(bool b) { m_range_checking = b; }

private:
    ShadingSystemImpl& m_shadingsys;  ///< Back-ptr to the shading system
    ShaderType m_shadertype;          ///< Type of shader
    std::string m_shadername;         ///< Shader name
    std::string m_osofilename;        ///< Full path of oso file
    OpcodeVec m_ops;                  ///< Actual code instructions
    std::vector<int> m_args;          ///< Arguments for all the ops
    // Need the code offsets for each code block
    SymbolVec m_symbols;                 ///< Symbols used by the shader
    std::vector<int> m_idefaults;        ///< int default param values
    std::vector<float> m_fdefaults;      ///< float default param values
    std::vector<ustring> m_sdefaults;    ///< string default param values
    std::vector<int> m_iconsts;          ///< int constant values
    std::vector<float> m_fconsts;        ///< float constant values
    std::vector<ustring> m_sconsts;      ///< string constant values
    int m_firstparam, m_lastparam;       ///< Subset of symbols that are params
    int m_maincodebegin, m_maincodeend;  ///< Main shader code range
    int m_raytype_queries;               ///< Bitmask of raytypes queried
    bool m_range_checking;  ///< Is range checking enabled for this shader?

    friend class OSOReaderToMaster;
    friend class ShaderInstance;
};



class ClosureRegistry {
public:
    struct ClosureEntry {
        // normally a closure is fully identified by its
        // name, but we might want to have an internal id
        // for fast dispatching
        int id;
        // The name again
        ustring name;
        // Number of formal arguments
        int nformal;
        // Number of keyword arguments
        int nkeyword;
        // The parameters
        std::vector<ClosureParam> params;
        // the needed size for the structure
        int struct_size;
        // Creation callbacks
        PrepareClosureFunc prepare;
        SetupClosureFunc setup;
    };

    void register_closure(string_view name, int id, const ClosureParam* params,
                          PrepareClosureFunc prepare, SetupClosureFunc setup);

    const ClosureEntry* get_entry(ustring name) const;
    const ClosureEntry* get_entry(int id) const
    {
        OSL_DASSERT((size_t)id < m_closure_table.size());
        return &m_closure_table[id];
    }

    bool empty() const { return m_closure_table.empty(); }

private:
    // A mapping from name to ID for the compiler
    std::map<ustring, int> m_closure_name_to_id;
    // And the internal global table, indexed
    // by the internal ID for fast dispatching
    std::vector<ClosureEntry> m_closure_table;
};



class ShadingSystemImpl {
public:
    ShadingSystemImpl(RendererServices* renderer   = NULL,
                      TextureSystem* texturesystem = NULL,
                      ErrorHandler* err            = NULL);
    ~ShadingSystemImpl();

    bool attribute(string_view name, TypeDesc type, const void* val);
    bool attribute(string_view name, int val)
    {
        return attribute(name, TypeDesc::INT, &val);
    }
    bool attribute(string_view name, float val)
    {
        return attribute(name, TypeDesc::FLOAT, &val);
    }
    bool attribute(string_view name, string_view val)
    {
        std::string valstr(val);
        const char* s = valstr.c_str();
        return attribute(name, TypeDesc::STRING, &s);
    }
    bool attribute(ShaderGroup* group, string_view name, TypeDesc type,
                   const void* val);
    bool getattribute(string_view name, TypeDesc type, void* val);
    bool getattribute(ShaderGroup* group, string_view name, TypeDesc type,
                      void* val);
    bool LoadMemoryCompiledShader(string_view shadername, string_view buffer);
    bool Parameter(ShaderGroup& group, string_view name, TypeDesc t,
                   const void* val, ParamHints props);
    bool Parameter(string_view name, TypeDesc t, const void* val,
                   ParamHints props);
    bool Shader(ShaderGroup& group, string_view shaderusage,
                string_view shadername, string_view layername);
    bool Shader(string_view shaderusage, string_view shadername,
                string_view layername);
    ShaderGroupRef ShaderGroupBegin(string_view groupname = string_view());
    bool ShaderGroupEnd(ShaderGroup& group);
    bool ShaderGroupEnd(void);
    bool ConnectShaders(ShaderGroup& group, string_view srclayer,
                        string_view srcparam, string_view dstlayer,
                        string_view dstparam);
    bool ConnectShaders(string_view srclayer, string_view srcparam,
                        string_view dstlayer, string_view dstparam);
    ShaderGroupRef ShaderGroupBegin(string_view groupname, string_view usage,
                                    string_view groupspec);
    bool ReParameter(ShaderGroup& group, string_view layername,
                     string_view paramname, TypeDesc type, const void* val);

    // Internal error, warning, info, and message reporting routines that
    // take std::format-like arguments.
    template<typename Str, typename... Args>
    inline void errorfmt(const Str& fmt, Args&&... args) const
    {
        error(fmtformat(fmt, std::forward<Args>(args)...));
    }
    void error(const std::string& message) const;

    template<typename Str, typename... Args>
    inline void warningfmt(const Str& fmt, Args&&... args) const
    {
        warning(fmtformat(fmt, std::forward<Args>(args)...));
    }
    void warning(const std::string& message) const;

    template<typename Str, typename... Args>
    inline void infofmt(const Str& fmt, Args&&... args) const
    {
        info(fmtformat(fmt, std::forward<Args>(args)...));
    }
    void info(const std::string& message) const;

    template<typename Str, typename... Args>
    inline void messagefmt(const Str& fmt, Args&&... args) const
    {
        message(fmtformat(fmt, std::forward<Args>(args)...));
    }
    void message(const std::string& message) const;

    std::string getstats(int level = 1) const;

    ErrorHandler& errhandler() const { return *m_err; }

    ShaderMaster::ref loadshader(string_view name);

    PerThreadInfo* create_thread_info();

    void destroy_thread_info(PerThreadInfo* threadinfo);

    ShadingContext*
    get_context(PerThreadInfo* threadinfo,
                TextureSystem::Perthread* texture_threadinfo = NULL);

    void release_context(ShadingContext* ctx);

    bool execute(ShadingContext& ctx, ShaderGroup& group, int thread_index,
                 int shadeindex, ShaderGlobals& ssg, void* userdata_base_ptr,
                 void* output_base_ptr, bool run = true);

    const void* get_symbol(ShadingContext& ctx, ustring layername,
                           ustring symbolname, TypeDesc& type);

    //    void operator delete (void *todel) { ::delete ((char *)todel); }

    /// Is the shading system in debug mode, and if so, how verbose?
    ///
    int debug() const { return m_debug; }

    /// Return a pointer to the renderer services object.
    ///
    RendererServices* renderer() const { return m_renderer; }

    /// Return a pointer to the texture system.
    ///
    TextureSystem* texturesys() const { return m_texturesys; }

    bool use_optix() const { return m_use_optix; }
    bool use_optix_cache() const { return m_use_optix_cache; }
    bool debug_nan() const { return m_debugnan; }
    bool debug_uninit() const { return m_debug_uninit; }
    bool lockgeom_default() const { return m_lockgeom_default; }
    bool strict_messages() const { return m_strict_messages; }
    bool range_checking() const { return m_range_checking; }
    bool unknown_coordsys_error() const
    {
        return m_shading_state_uniform.m_unknown_coordsys_error;
    }
    bool connection_error() const { return m_connection_error; }
    bool relaxed_param_typecheck() const { return m_relaxed_param_typecheck; }
    int optimize() const { return m_optimize; }
    int llvm_optimize() const { return m_llvm_optimize; }
    int llvm_debug() const { return m_llvm_debug; }
    int llvm_debug_layers() const { return m_llvm_debug_layers; }
    int llvm_debug_ops() const { return m_llvm_debug_ops; }
    int llvm_target_host() const { return m_llvm_target_host; }
    int llvm_debugging_symbols() const { return m_llvm_debugging_symbols; }
    int llvm_profiling_events() const { return m_llvm_profiling_events; }
    int llvm_output_bitcode() const { return m_llvm_output_bitcode; }
    bool dump_forced_llvm_bool_symbols() const
    {
        return m_dump_forced_llvm_bool_symbols;
    }
    bool dump_uniform_symbols() const { return m_dump_uniform_symbols; }
    bool dump_varying_symbols() const { return m_dump_varying_symbols; }
    ustring llvm_prune_ir_strategy() const { return m_llvm_prune_ir_strategy; }
    bool fold_getattribute() const { return m_opt_fold_getattribute; }
    bool opt_texture_handle() const { return m_opt_texture_handle; }
    int opt_passes() const { return m_opt_passes; }
    int max_warnings_per_thread() const
    {
        return m_shading_state_uniform.m_max_warnings_per_thread;
    }
    bool countlayerexecs() const { return m_countlayerexecs; }
    bool lazy_userdata() const { return m_lazy_userdata; }
    bool lazy_trace() const { return m_lazy_trace; }
    bool userdata_isconnected() const { return m_userdata_isconnected; }
    int profile() const { return m_profile; }
    bool no_noise() const { return m_no_noise; }
    bool no_pointcloud() const { return m_no_pointcloud; }
    bool force_derivs() const { return m_force_derivs; }
    bool allow_shader_replacement() const { return m_allow_shader_replacement; }
    ustring commonspace_synonym() const
    {
        return ustring_from(m_shading_state_uniform.m_commonspace_synonym);
    }

    bool llvm_jit_fma() const { return m_llvm_jit_fma; }
    ustring llvm_jit_target() const { return m_llvm_jit_target; }

    ustring debug_groupname() const { return m_debug_groupname; }
    ustring debug_layername() const { return m_debug_layername; }

    std::string library_searchpath() const { return m_library_searchpath; }
    const std::vector<std::string>& library_searchpath_dirs() const
    {
        return m_library_searchpath_dirs;
    }

    /// Look within the group for separate nodes that are actually
    /// duplicates of each other and combine them.  Return the number of
    /// instances that were eliminated.
    int merge_instances(ShaderGroup& group, bool post_opt = false);

    /// The group is set and won't be changed again; take advantage of
    /// this by optimizing the code knowing all our instance parameters
    /// (at least the ones that can't be overridden by the geometry).
    void optimize_group(ShaderGroup& group, ShadingContext* ctx, bool do_jit);

    /// After doing all optimization and code JIT, we can clean up by
    /// deleting the instances' code and arguments, and paring their
    /// symbol tables down to just parameters.
    void group_post_jit_cleanup(ShaderGroup& group);

    int* alloc_int_constants(size_t n) { return m_int_pool.alloc(n); }
    float* alloc_float_constants(size_t n) { return m_float_pool.alloc(n); }
    ustring* alloc_string_constants(size_t n) { return m_string_pool.alloc(n); }

    void register_closure(string_view name, int id, const ClosureParam* params,
                          PrepareClosureFunc prepare, SetupClosureFunc setup);
    bool query_closure(const char** name, int* id, const ClosureParam** params);
    const ClosureRegistry::ClosureEntry* find_closure(ustring name) const
    {
        return m_closure_registry.get_entry(name);
    }
    const ClosureRegistry::ClosureEntry* find_closure(int id) const
    {
        return m_closure_registry.get_entry(id);
    }

    /// Attributes to control optimization for OptiX/CUDA
    bool optix_no_inline() const { return m_optix_no_inline; }
    bool optix_no_inline_layer_funcs() const
    {
        return m_optix_no_inline_layer_funcs;
    }
    bool optix_merge_layer_funcs() const { return m_optix_merge_layer_funcs; }
    bool optix_no_inline_rend_lib() const { return m_optix_no_inline_rend_lib; }
    int optix_no_inline_thresh() const { return m_optix_no_inline_thresh; }
    int optix_force_inline_thresh() const
    {
        return m_optix_force_inline_thresh;
    }


    OSLEXECPUBLIC int raytype_bit(ustring name);

    void optimize_all_groups(int nthreads = 0, int mythread = 0,
                             int totalthreads = 1, bool do_jit = true);

    typedef std::unordered_map<ustring, OpDescriptor> OpDescriptorMap;

    /// Look up OpDescriptor for the named op, return NULL for unknown op.
    ///
    const OpDescriptor* op_descriptor(ustring opname)
    {
        OpDescriptorMap::const_iterator i = m_op_descriptor.find(opname);
        if (i != m_op_descriptor.end())
            return &(i->second);
        else
            return NULL;
    }

    void pointcloud_stats(int search, int get, int results, int writes = 0)
    {
        m_stat_pointcloud_searches += search;
        m_stat_pointcloud_gets += get;
        m_stat_pointcloud_searches_total_results += results;
        if (search && !results)
            ++m_stat_pointcloud_failures;
        OIIO::atomic_max(m_stat_pointcloud_max_results, results);
        m_stat_pointcloud_writes += writes;
    }

    /// Is the named symbol among the renderer outputs?
    bool is_renderer_output(ustring layername, ustring paramname,
                            ShaderGroup* group) const;

    /// Serialize the entire group, including oso files, into a compressed
    /// archive.
    bool archive_shadergroup(ShaderGroup& group, string_view filename);

    void count_noise(int number = 1) { m_stat_noise_calls += number; }

    ColorSystem& colorsystem() { return m_shading_state_uniform.m_colorsystem; }

    std::shared_ptr<OIIO::ColorConfig> colorconfig();

#if OSL_USE_BATCHED
    // Group all batched methods behind a templated interface
    // so we can support multiple widths
    template<int WidthT> class Batched {
        ShadingSystemImpl& m_ssi;

    public:
        explicit OSL_FORCEINLINE Batched(ShadingSystemImpl& ssi) : m_ssi(ssi) {}
        OSL_FORCEINLINE Batched(const Batched&) = default;

        /// Ensure that the group has been JITed.
        void jit_group(ShaderGroup& group, ShadingContext* ctx);

        void jit_all_groups(int nthreads = 0, int mythread = 0,
                            int totalthreads = 1);
    };

    template<int WidthT> OSL_FORCEINLINE Batched<WidthT> batched()
    {
        return Batched<WidthT>(*this);
    }
#endif

    void clear_symlocs() { m_symlocs.clear(); }
    void add_symlocs(cspan<SymLocationDesc> symlocs)
    {
        for (auto& s : symlocs)
            m_symlocs.push_back(s);
    }
    const SymLocationDesc* find_symloc(ustring name) const
    {
        auto f = std::lower_bound(m_symlocs.begin(), m_symlocs.end(), name);
        return (f == m_symlocs.end() || f->name != name) ? nullptr : &(*f);
    }
    const SymLocationDesc* find_symloc(ustring name, SymArena arena) const
    {
        auto f = std::lower_bound(m_symlocs.begin(), m_symlocs.end(), name);
        if (f != m_symlocs.end() && f->name == name && f->arena == arena
            && f->offset != -1)
            return &(*f);
        else
            return nullptr;
    }

    void register_inline_function(ustring name);
    void unregister_inline_function(ustring name);
    void register_noinline_function(ustring name);
    void unregister_noinline_function(ustring name);


private:
    void printstats() const;

    /// Find the index of the named layer in the shader group.
    /// If found, return the index >= 0 and put a pointer to the instance
    /// in inst; if not found, return -1 and set inst to NULL.
    /// (This is a helper for ConnectShaders.)
    int find_named_layer_in_group(ShaderGroup& group, ustring layername,
                                  ShaderInstance*& inst);

    /// Turn a connectionname (such as "Kd" or "Cout[1]", etc.) into a
    /// ConnectedParam descriptor.  This routine is strictly a helper for
    /// ConnectShaders, and will issue error messages on its behalf.
    /// The return value will not be valid() if there is an error.
    ConnectedParam decode_connected_param(string_view connectionname,
                                          string_view layername,
                                          ShaderInstance* inst);

    /// Set up LLVM -- make sure we have a Context, Module, ExecutionEngine,
    /// retained JITMemoryManager, etc.
    void SetupLLVM();

    void setup_op_descriptors();

    RendererServices* m_renderer;  ///< Renderer services
    std::shared_ptr<TextureSystem> m_texturesys_sp;
    TextureSystem* m_texturesys;  ///< Texture system

    ErrorHandler* m_err;  ///< Error handler
    mutable std::list<std::string> m_errseen, m_warnseen;
    static const int m_errseenmax = 32;
    mutable mutex m_errmutex;

    typedef std::map<ustring, ShaderMaster::ref> ShaderNameMap;
    ShaderNameMap m_shader_masters;  ///< name -> shader masters map

    ConstantPool<int> m_int_pool;
    ConstantPool<Float> m_float_pool;
    ConstantPool<ustring> m_string_pool;

    OpDescriptorMap m_op_descriptor;

    // Pre-compiled support library
    std::vector<char>
        m_lib_bitcode;  ///> Container for the pre-compiled library bitcode

    std::vector<char>
        m_rs_bitcode;  ///> Container for the pre-compiled renderer services free function bitcode

    // Options
    int m_statslevel;             ///< Statistics level
    bool m_lazylayers;            ///< Evaluate layers on demand?
    bool m_lazyglobals;           ///< Run lazily even if globals write?
    bool m_lazyunconnected;       ///< Run lazily even if not connected?
    bool m_lazyerror;             ///< Run lazily even if it has error op
    bool m_lazy_userdata;         ///< Retrieve userdata lazily?
    bool m_lazy_trace;            ///< Run lazily even if it has trace call
    bool m_userdata_isconnected;  ///< Userdata params isconnected()?
    bool m_clearmemory;           ///< Zero mem before running shader?
    bool m_debugnan;              ///< Root out NaN's?
    bool m_debug_uninit;          ///< Find use of uninitialized vars?
    bool m_lockgeom_default;      ///< Default value of lockgeom
    bool m_strict_messages;       ///< Strict checking of message passing usage?
    bool m_error_repeats;         ///< Allow repeats of identical err/warn?
    bool m_range_checking;        ///< Range check arrays & components?
    bool m_connection_error;      ///< Error for ConnectShaders to fail?
    bool m_greedyjit;             ///< JIT as much as we can?
    bool m_countlayerexecs;       ///< Count number of layer execs?
    bool m_relaxed_param_typecheck;  ///< Allow parameters to be set from isomorphic types (same data layout)
    int m_profile;                 ///< Level of profiling of shader execution
    int m_optimize;                ///< Runtime optimization level
    bool m_opt_simplify_param;     ///< Turn instance params into const?
    bool m_opt_constant_fold;      ///< Allow constant folding?
    bool m_opt_stale_assign;       ///< Optimize stale assignments?
    bool m_opt_elide_useless_ops;  ///< Optimize away useless ops?
    bool m_opt_elide_unconnected_outputs;  ///< Elide unconnected outputs?
    bool m_opt_peephole;                   ///< Do some peephole optimizations?
    bool m_opt_coalesce_temps;             ///< Coalesce temporary variables?
    bool m_opt_assign;                     ///< Do various assign optimizations?
    bool m_opt_mix;                        ///< Special 'mix' optimizations
    char m_opt_merge_instances;            ///< Merge identical instances?
    bool m_opt_merge_instances_with_userdata;  ///< Merge identical instances if they have userdata?
    bool m_opt_fold_getattribute;    ///< Constant-fold getattribute()?
    bool m_opt_middleman;            ///< Middle-man optimization?
    bool m_opt_texture_handle;       ///< Use texture handles?
    bool m_opt_seed_bblock_aliases;  ///< Turn on basic block alias seeds
    bool m_opt_useparam;  ///< Perform extra useparam analysis for culling run layer calls
    bool m_opt_groupdata;  ///< Move eligible parameters out of groupdata into locals
    bool m_opt_batched_analysis;  ///< Perform extra analysis required for batched execution?
    bool m_llvm_jit_fma;         ///< Allow fused multiply/add in JIT
    bool m_llvm_jit_aggressive;  ///< Turn on llvm "aggressive" JIT
    bool m_optimize_nondebug;    ///< Fully optimize non-debug!
    ustring m_llvm_jit_target;   ///< ISA target for JIT
    int m_vector_width;          ///< SIMD width maximum (8)
    int m_opt_passes;            ///< Opt passes per layer
    int m_llvm_optimize;         ///< OSL optimization strategy
    int m_debug;                 ///< Debugging output
    int m_llvm_debug;            ///< More LLVM debugging output
    int m_llvm_debug_layers;     ///< Add layer enter/exit printfs
    int m_llvm_debug_ops;        ///< Add printfs to every op
    int m_llvm_target_host;      ///< Target specific host architecture
    int m_llvm_debugging_symbols;  ///< Generate GDB compatible debug info during JIT
    int m_llvm_profiling_events;  ///< Emit Intel profiling events during JIT
    int m_llvm_output_bitcode;    ///< Output bitcode for each group
    int m_llvm_dumpasm;           ///< Output CPU asm of the JIT
    bool m_dump_forced_llvm_bool_symbols;  ///< Output symbols BatchedAnalsysis determined could be forced to be an llvm boolean
    bool m_dump_uniform_symbols;  ///< Output symbols BatchedAnalsysis determined are uniform
    bool m_dump_varying_symbols;  ///< Output symbols BatchedAnalsysis determined are varying
    ustring m_llvm_prune_ir_strategy;  ///< LLVM IR pruning strategy
    ustring m_debug_groupname;         ///< Name of sole group to debug
    ustring m_debug_layername;         ///< Name of sole layer to debug
    ustring m_opt_layername;           ///< Name of sole layer to optimize
    ustring m_only_groupname;          ///< Name of sole group to compile
    ustring m_archive_groupname;       ///< Name of group to pickle/archive
    ustring m_archive_filename;        ///< Name of filename for group archive
    std::string m_searchpath;          ///< Shader search path
    std::vector<std::string> m_searchpath_dirs;  ///< All searchpath dirs
    std::string m_library_searchpath;            ///< Library search path
    std::vector<std::string>
        m_library_searchpath_dirs;            ///< All library searchpath dirs
    std::vector<ustring> m_raytypes;          ///< Names of ray types
    std::vector<ustring> m_renderer_outputs;  ///< Names of renderer outputs
    std::vector<SymLocationDesc> m_symlocs;
    int m_max_local_mem_KB;  ///< Local storage can a shader use
    int m_compile_report;    ///< Print compilation report?
    bool m_use_optix;        ///< This is an OptiX-based renderer
    bool m_use_optix_cache;  ///< Renderer-enabled caching for OptiX ptx
    int m_max_optix_groupdata_alloc;  ///< Maximum OptiX groupdata buffer allocation
    bool m_buffer_printf;             ///< Buffer/batch printf output?
    bool m_no_noise;                  ///< Substitute trivial noise calls
    bool m_no_pointcloud;             ///< Substitute trivial pointcloud calls
    bool m_force_derivs;              ///< Force derivs on everything
    bool m_allow_shader_replacement;  ///< Allow shader masters to replace
    int m_exec_repeat;                ///< How many times to execute group
    int m_opt_warnings;               ///< Warn on inability to optimize
    int m_gpu_opt_error;              ///< Error on inability to optimize
                                      ///<   away things that can't GPU.

    /// Experimental attributes to help tuning OptiX optimization passes
    bool m_optix_no_inline;              ///< Disable function inlining
    bool m_optix_no_inline_layer_funcs;  ///< Disable inlining for group layer funcs
    bool m_optix_merge_layer_funcs;  ///< Merge layer functions that have only one caller
    bool m_optix_no_inline_rend_lib;  ///< Disable inlining the rend_lib functions
    int m_optix_no_inline_thresh;  ///< Disable inlining for functions larger than the threshold
    int m_optix_force_inline_thresh;  ///< Force inling for functions smaller than the threshold

    ustring m_colorspace;  ///< What RGB colors mean

    ShadingStateUniform m_shading_state_uniform;

    std::shared_ptr<OIIO::ColorConfig>
        m_colorconfig;  ///< OIIO/OCIO color configuration

    // Thread safety
    mutable mutex m_mutex;

    // Stats
    atomic_int m_stat_shaders_loaded;      ///< Stat: shaders loaded
    atomic_int m_stat_shaders_requested;   ///< Stat: shaders requested
    PeakCounter<int> m_stat_instances;     ///< Stat: instances
    PeakCounter<int> m_stat_contexts;      ///< Stat: shading contexts
    atomic_int m_stat_groups;              ///< Stat: shading groups
    atomic_int m_stat_groupinstances;      ///< Stat: total inst in all groups
    atomic_int m_stat_instances_compiled;  ///< Stat: instances compiled
    atomic_int m_stat_groups_compiled;     ///< Stat: groups compiled
    atomic_int m_stat_empty_instances;     ///< Stat: shaders empty after opt
    atomic_int m_stat_merged_inst;         ///< Stat: number of merged instances
    atomic_int m_stat_merged_inst_opt;     ///< Stat: merged insts after opt
    atomic_int m_stat_empty_groups;        ///< Stat: groups empty after opt
    atomic_int m_stat_regexes;             ///< Stat: how many regex's compiled
    atomic_int m_stat_preopt_syms;         ///< Stat: pre-optimization symbols
    atomic_int m_stat_postopt_syms;        ///< Stat: post-optimization symbols
    atomic_int m_stat_syms_with_derivs;    ///< Stat: post-opt syms with derivs
    atomic_int m_stat_preopt_ops;          ///< Stat: pre-optimization ops
    atomic_int m_stat_postopt_ops;         ///< Stat: post-optimization ops
    atomic_int m_stat_middlemen_eliminated;  ///< Stat: middlemen eliminated
    atomic_int m_stat_const_connections;     ///< Stat: const connections elim'd
    atomic_int m_stat_global_connections;   ///< Stat: global connections elim'd
    atomic_int m_stat_tex_calls_codegened;  ///< Stat: total texture calls
    atomic_int m_stat_tex_calls_as_handles;  ///< Stat: texture calls with handles
    atomic_int m_stat_useparam_ops;  ///< Stat: pre-optimization useparam ops
    atomic_int m_stat_call_layers_inserted;  ///< Stat: post-opt layer calls
    double m_stat_master_load_time;          ///< Stat: time loading masters
    double m_stat_optimization_time;         ///< Stat: time spent optimizing
    double m_stat_opt_locking_time;          ///<   locking time
    double m_stat_specialization_time;       ///<   runtime specialization time
    double m_stat_total_llvm_time;           ///<   total time spent on LLVM
    double m_stat_llvm_setup_time;           ///<     llvm setup time
    double m_stat_llvm_irgen_time;           ///<     llvm IR generation time
    double m_stat_llvm_opt_time;             ///<     llvm IR optimization time
    double m_stat_llvm_jit_time;             ///<     llvm JIT time
    double m_stat_inst_merge_time;           ///< Stat: time merging instances
    double m_stat_getattribute_time;       ///< Stat: time spent in getattribute
    double m_stat_getattribute_fail_time;  ///< Stat: time spent in getattribute
    atomic_ll m_stat_getattribute_calls;   ///< Stat: Number of getattribute
    atomic_ll m_stat_get_userdata_calls;   ///< Stat: # of get_userdata calls
    atomic_ll m_stat_noise_calls;          ///< Stat: # of noise calls
    atomic_ll m_stat_pointcloud_searches;
    atomic_ll m_stat_pointcloud_searches_total_results;
    atomic_int m_stat_pointcloud_max_results;
    atomic_int m_stat_pointcloud_failures;
    atomic_ll m_stat_pointcloud_gets;
    atomic_ll m_stat_pointcloud_writes;
    atomic_ll m_stat_layers_executed;           ///< Total layers executed
    atomic_ll m_stat_total_shading_time_ticks;  ///< Total shading time (ticks)
    atomic_ll m_stat_reparam_calls_total;
    atomic_ll m_stat_reparam_bytes_total;
    atomic_ll m_stat_reparam_calls_changed;
    atomic_ll m_stat_reparam_bytes_changed;

    int m_stat_max_llvm_local_mem;     ///< Stat: max LLVM local mem
    PeakCounter<off_t> m_stat_memory;  ///< Stat: all shading system memory

    PeakCounter<off_t> m_stat_mem_master;  ///< Stat: master-related mem
    PeakCounter<off_t> m_stat_mem_master_ops;
    PeakCounter<off_t> m_stat_mem_master_args;
    PeakCounter<off_t> m_stat_mem_master_syms;
    PeakCounter<off_t> m_stat_mem_master_defaults;
    PeakCounter<off_t> m_stat_mem_master_consts;
    PeakCounter<off_t> m_stat_mem_inst;  ///< Stat: instance-related mem
    PeakCounter<off_t> m_stat_mem_inst_syms;
    PeakCounter<off_t> m_stat_mem_inst_paramvals;
    PeakCounter<off_t> m_stat_mem_inst_connections;

    mutable spin_mutex m_stat_mutex;  ///< Mutex for non-atomic stats
    ClosureRegistry m_closure_registry;
    std::vector<std::weak_ptr<ShaderGroup>> m_all_shader_groups;
    mutable spin_mutex m_all_shader_groups_mutex;

    // State for entering shader groups -- this is only for the
    // non-threadsafe calls to Parameter/etc that don't take a group
    // reference.
    ShaderGroupRef m_curgroup;

    atomic_int m_groups_to_compile_count;
    atomic_int m_threads_currently_compiling;
    mutable std::map<ustring, long long> m_group_profile_times;
    // N.B. group_profile_times is protected by m_stat_mutex.

    LLVM_Util::ScopedJitMemoryUser m_llvm_jit_memory_user;

    std::unordered_set<ustring> m_inline_functions;
    std::unordered_set<ustring> m_noinline_functions;

    friend class OSL::ShadingContext;
    friend class ShaderMaster;
    friend class ShaderInstance;
    friend class RuntimeOptimizer;
    friend class BackendLLVM;
#if OSL_USE_BATCHED
    friend class BatchedBackendLLVM;
#endif
};



/// Describe one end of a parameter connection: the parameter number, and
/// optionally an array index and/or channel number within that parameter.
struct ConnectedParam {
    int param;            ///< Parameter number (in the symbol table)
    int arrayindex : 27;  ///< Array index (-1 for not an index)
    int channel : 5;      ///< Channel number (-1 for no channel selection)
    TypeSpec type;        ///< Type of data being connected
    // N.B. Use bitfields to squeeze the structure down by 4 bytes.
    // Consequence is that you can't connect individual elements of
    // arrays with more than 2^26 (32M) elements. Somehow I don't think
    // that's going to be a limitation to worry about.
    ConnectedParam() : param(-1), arrayindex(-1), channel(-1) {}

    bool valid() const { return (param >= 0); }

    bool operator==(const ConnectedParam& p) const
    {
        return param == p.param && arrayindex == p.arrayindex
               && channel == p.channel && type == p.type;
    }
    bool operator!=(const ConnectedParam& p) const
    {
        return param != p.param || arrayindex != p.arrayindex
               || channel != p.channel || type != p.type;
    }

    // Is it a complete connection, not partial?
    bool is_complete() const { return arrayindex == -1 && channel == -1; }

    // Debug output of ConnectedParam
    std::string str(const ShaderInstance* inst, bool unmangle) const;
};



/// Describe a parameter connection to an earlier layer.
///
struct Connection {
    int srclayer;        ///< Layer (within our group) of the source
    ConnectedParam src;  ///< Which source parameter (or part thereof)
    ConnectedParam dst;  ///< Which destination parameter (or part thereof)

    Connection(int srclay, const ConnectedParam& srccon,
               const ConnectedParam& dstcon)
        : srclayer(srclay), src(srccon), dst(dstcon)
    {
    }
    bool operator==(const Connection& c) const
    {
        return srclayer == c.srclayer && src == c.src && dst == c.dst;
    }
    bool operator!=(const Connection& c) const
    {
        return srclayer != c.srclayer || src != c.src || dst != c.dst;
    }

    // Does the connection fully join the source and destination.
    bool is_complete() const { return src.is_complete() && dst.is_complete(); }

    // Debug output of ConnectedParam
    std::string str(const ShaderGroup& group, const ShaderInstance* dstinst,
                    bool unmangle = true) const;
};



typedef std::vector<Connection> ConnectionVec;


/// Macro to loop over just the params & output params of an instance,
/// with each iteration providing a Symbol& to symbolref.  Use like this:
///        FOREACH_PARAM (Symbol &s, inst) { ... stuff with s... }
///
#define FOREACH_PARAM(symboldecl, inst) for (symboldecl : param_range(inst))

#define FOREACH_SYM(symboldecl, inst) for (symboldecl : sym_range(inst))



/// ShaderInstance is a particular instance of a shader, with its own
/// set of parameter values, coordinate transform, and connections to
/// other instances within the same shader group.
class ShaderInstance {
public:
    typedef ShaderInstanceRef ref;
    ShaderInstance(ShaderMaster::ref master,
                   string_view layername = string_view());
    ShaderInstance(const ShaderInstance& copy);  // not defined
    ~ShaderInstance();

    /// Return the layer name of this instance
    ///
    ustring layername() const { return m_layername; }

    /// Return the name of the shader used by this instance.
    ///
    const std::string& shadername() const { return m_master->shadername(); }

    /// Return a pointer to the master for this instance.
    ///
    ShaderMaster* master() const { return m_master.get(); }

    /// Return a reference to the shading system for this instance.
    ///
    ShadingSystemImpl& shadingsys() const { return m_master->shadingsys(); }

    /// Apply pending parameters
    void parameters(const ParamValueList& params, cspan<ParamHints> hints);

    /// Find the named symbol, return its index in the symbol array, or
    /// -1 if not found.
    int findsymbol(ustring name) const;

    /// Find the named parameter, return its index in the symbol array, or
    /// -1 if not found.
    int findparam(ustring name, bool search_master = true) const;

    /// Return a pointer to the symbol (specified by integer index),
    /// or NULL (if index was -1, as returned by 'findsymbol').
    Symbol* symbol(int index)
    {
        return index >= 0 && index < (int)m_instsymbols.size()
                   ? &m_instsymbols[index]
                   : NULL;
    }
    const Symbol* symbol(int index) const
    {
        return index >= 0 && index < (int)m_instsymbols.size()
                   ? &m_instsymbols[index]
                   : NULL;
    }

    /// Given symbol pointer, what is its index in the table?
    int symbolindex(Symbol* s) { return s - &m_instsymbols[0]; }

    /// Return a pointer to the master's version of the indexed symbol.
    /// It's a const*, since you shouldn't mess with the master's copy.
    const Symbol* mastersymbol(int index) const
    {
        return index >= 0 ? master()->symbol(index) : NULL;
    }

    /// Where is the location that holds the parameter's instance value?
    void* param_storage(int index);
    const void* param_storage(int index) const;

    /// Add a connection
    ///
    void add_connection(int srclayer, const ConnectedParam& srccon,
                        const ConnectedParam& dstcon);

    /// How many connections to earlier layers do we have?
    ///
    int nconnections() const { return (int)m_connections.size(); }

    /// Return a reference to the i-th connection to an earlier layer.
    ///
    const Connection& connection(int i) const { return m_connections[i]; }
    Connection& connection(int i) { return m_connections[i]; }

    /// Reference to the connection list.
    ///
    ConnectionVec& connections() { return m_connections; }
    const ConnectionVec& connections() const { return m_connections; }

    /// Free all the connection data, return the amount of memory they
    /// previously consumed.
    size_t clear_connections()
    {
        size_t mem = vectorbytes(m_connections);
        ConnectionVec().swap(m_connections);
        return mem;
    }

    /// Return the unique ID of this instance.
    ///
    int id() const { return m_id; }

    /// Does this instance potentially write to any global vars?
    bool writes_globals() const { return m_writes_globals; }
    void writes_globals(bool val) { m_writes_globals = val; }

    /// Does this instance potentially read userdata to initialize any of
    /// its parameters?
    bool userdata_params() const { return m_userdata_params; }
    void userdata_params(bool val) { m_userdata_params = val; }

    /// Does this instance potentially read userdata to initialize any of
    /// its parameters?
    bool has_error_op() const { return m_has_error_op; }
    void has_error_op(bool val) { m_has_error_op = val; }

    bool has_trace_op() const { return m_has_trace_op; }
    void has_trace_op(bool val) { m_has_trace_op = val; }

    /// Should this instance only be run lazily (i.e., not
    /// unconditionally)?
    bool run_lazily() const
    {
        // if lazy is turned off entirely, nobody can be lazy
        if (!shadingsys().m_lazylayers)
            return false;
        // Main group entry is never lazy
        if (last_layer())
            return false;
        // Shaders that set globals are not lazy unless lazyglobals is on
        if (writes_globals() && !shadingsys().m_lazyglobals)
            return false;
        // Shaders that write renderer outputs (AOVs) can't be lazy
        if (renderer_outputs())
            return false;
        // Shaders without any downstream connections are not lazy unless
        // lazyunconnected is on.
        if (!outgoing_connections() && !shadingsys().m_lazyunconnected)
            return false;
        // Shaders with error ops are not lazy unless lazyerror is on.
        if (!shadingsys().m_lazyerror && has_error_op())
            return false;

        if (!shadingsys().lazy_trace() && has_trace_op())
            return false;

        return true;
    }

    bool last_layer() const { return m_last_layer; }
    void last_layer(bool last) { m_last_layer = last; }

    /// Does this instance have any outgoing connections?
    ///
    bool outgoing_connections() const { return m_outgoing_connections; }
    /// Set whether this instance has outgoing connections.
    ///
    void outgoing_connections(bool out) { m_outgoing_connections = out; }

    /// Does this instance have any renderer outputs?
    bool renderer_outputs() const { return m_renderer_outputs; }
    /// Set whether this instance has renderer outputs
    void renderer_outputs(bool out) { m_renderer_outputs = out; }

    /// Is this instance a callable entry point for the group?
    bool entry_layer() const { return m_entry_layer; }
    void entry_layer(bool val) { m_entry_layer = val; }

    /// Was this instance merged away and now no longer needed?
    bool merged_unused() const { return m_merged_unused; }

    int maincodebegin() const { return m_maincodebegin; }
    int maincodeend() const { return m_maincodeend; }

    int firstparam() const { return m_firstparam; }
    int lastparam() const { return m_lastparam; }

    // Range type suitable for use with "range for"
    template<typename T>  // T should be Symbol or const Symbol
    struct SymRange {
        SymRange() : m_begin(nullptr), m_end(nullptr) {}
        SymRange(T* a, T* b) : m_begin(a), m_end(b) {}
        T* begin() const { return m_begin; }
        T* end() const { return m_end; }

    private:
        T* m_begin;
        T* m_end;
    };

    /// Return a SymRange for the set of param symbols that is suitable to
    /// pass as a "range for".
    friend SymRange<Symbol> param_range(ShaderInstance* i)
    {
        if (i->m_instsymbols.size() == 0 || i->firstparam() == i->lastparam())
            return SymRange<Symbol>();
        else
            return SymRange<Symbol>(&i->m_instsymbols[0] + i->firstparam(),
                                    &i->m_instsymbols[0] + i->lastparam());
    }

    friend SymRange<const Symbol> param_range(const ShaderInstance* i)
    {
        if (i->m_instsymbols.size() == 0 || i->firstparam() == i->lastparam())
            return SymRange<const Symbol>();
        else
            return SymRange<const Symbol>(&i->m_instsymbols[0]
                                              + i->firstparam(),
                                          &i->m_instsymbols[0]
                                              + i->lastparam());
    }

    friend SymRange<Symbol> sym_range(ShaderInstance* i)
    {
        if (i->m_instsymbols.size() == 0)
            return SymRange<Symbol>();
        else
            return SymRange<Symbol>(&i->m_instsymbols[0],
                                    &i->m_instsymbols[0]
                                        + i->m_instsymbols.size());
    }
    friend SymRange<const Symbol> sym_range(const ShaderInstance* i)
    {
        if (i->m_instsymbols.size() == 0)
            return SymRange<const Symbol>();
        else
            return SymRange<const Symbol>(&i->m_instsymbols[0],
                                          &i->m_instsymbols[0]
                                              + i->m_instsymbols.size());
    }

    int Psym() const { return m_Psym; }
    int Nsym() const { return m_Nsym; }

    const std::vector<int>& args() const { return m_instargs; }
    std::vector<int>& args() { return m_instargs; }
    int arg(int argnum) const { return args()[argnum]; }
    const Symbol* argsymbol(int argnum) const { return symbol(arg(argnum)); }
    Symbol* argsymbol(int argnum) { return symbol(arg(argnum)); }
    const OpcodeVec& ops() const { return m_instops; }
    OpcodeVec& ops() { return m_instops; }
    const Opcode& op(int opnum) const { return ops()[opnum]; }
    Opcode& op(int opnum) { return ops()[opnum]; }
    SymbolVec& symbols() { return m_instsymbols; }
    const SymbolVec& symbols() const { return m_instsymbols; }

    /// Make sure there's room for more symbols.
    ///
    void make_symbol_room(size_t moresyms = 1);

    /// Does it appear that the layer is completely unused?
    ///
    bool unused() const
    {
        // Entry layers are used no matter what
        if (last_layer() || entry_layer())
            return false;
        // It will be used if it has outgoing connections or sets renderer
        // outputs, or if it sets globals (but only if lazyglobals is off),
        // or if it has no downstream connections (but only if
        // lazyunconnected is off).
        bool used
            = (outgoing_connections() || renderer_outputs()
               || (writes_globals() && !shadingsys().m_lazyglobals)
               || (!outgoing_connections() && !shadingsys().m_lazyunconnected)
               || (!shadingsys().m_lazyerror && has_error_op()));
        return !used || merged_unused();
    }

    /// Is the instance reduced to nothing but an 'end' instruction and no
    /// symbols?
    bool empty_instance() const
    {
        return (
            symbols().size() == 0
            && (ops().size() == 0 ||
#ifdef __CUDA_ARCH__
                // TODO: is this ever run on a device, why special case it?
                (ops().size() == 1
                 && ustringhash_from(OSL::strhash(ops()[0].opname().c_str()))
                        == Hashes::end)
#else
                (ops().size() == 1 && ops()[0].opname() == Strings::end)
#endif
                    ));
    }

    /// Make our own version of the code and args from the master.
    void copy_code_from_master(ShaderGroup& group);

    /// Check the params to re-assess writes_globals and userdata_params.
    /// Sorry, can't think of a short name that isn't too cryptic.
    void evaluate_writes_globals_and_userdata_params();

    /// Small data structure to hold just the symbol info that the
    /// instance overrides from the master copy.
    struct SymOverrideInfo {
        // Using bit fields to keep the data in 8 bytes in total.
        // Note: it's important that all the bitfields are the same type
        // (unsigned int), or MSVS won't merge them properly into one int.
        unsigned int m_valuesource : 3;
        unsigned int m_connected_down : 1;
        unsigned int m_interpolated : 1;
        unsigned int m_interactive : 1;
        unsigned int m_arraylen : 26;
        int m_data_offset;

        SymOverrideInfo()
            : m_valuesource(Symbol::DefaultVal)
            , m_connected_down(false)
            , m_interpolated(false)
            , m_interactive(false)
            , m_arraylen(0)
            , m_data_offset(0)
        {
        }
        void valuesource(Symbol::ValueSource v) { m_valuesource = v; }
        Symbol::ValueSource valuesource() const
        {
            return (Symbol::ValueSource)m_valuesource;
        }
        const char* valuesourcename() const
        {
            return Symbol::valuesourcename(valuesource());
        }
        bool connected_down() const { return m_connected_down; }
        void connected_down(bool c) { m_connected_down = c; }
        bool connected() const { return valuesource() == Symbol::ConnectedVal; }
        bool interpolated() const { return m_interpolated; }
        void interpolated(bool val) { m_interpolated = val; }
        bool interactive() const { return m_interactive; }
        void interactive(bool val) { m_interactive = val; }
        int arraylen() const { return m_arraylen; }
        void arraylen(int s) { m_arraylen = s; }
        int dataoffset() const { return m_data_offset; }
        void dataoffset(int o) { m_data_offset = o; }
        friend bool equivalent(const SymOverrideInfo& a,
                               const SymOverrideInfo& b)
        {
            return a.valuesource() == b.valuesource()
                   && a.interpolated() == b.interpolated()
                   && a.interactive() == b.interactive()
                   && a.arraylen() == b.arraylen();
        }
    };
    typedef std::vector<SymOverrideInfo> SymOverrideInfoVec;
    static_assert(sizeof(SymOverrideInfo) == 8, "SymOverrideInfo size");

    SymOverrideInfo* instoverride(int i) { return &m_instoverrides[i]; }
    const SymOverrideInfo* instoverride(int i) const
    {
        return &m_instoverrides[i];
    }

    /// Are two shader instances (assumed to be in the same group)
    /// equivalent, in that they may be merged into a single instance?
    bool mergeable(const ShaderInstance& b, const ShaderGroup& g) const;

private:
    ShaderMaster::ref m_master;          ///< Reference to the master
    SymOverrideInfoVec m_instoverrides;  ///< Instance parameter info
    SymbolVec m_instsymbols;             ///< Symbols used by the instance
    OpcodeVec m_instops;                 ///< Actual code instructions
    std::vector<int> m_instargs;         ///< Arguments for all the ops
    ustring m_layername;                 ///< Name of this layer
    std::vector<int> m_iparams;          ///< int param values
    std::vector<float> m_fparams;        ///< float param values
    std::vector<ustring> m_sparams;      ///< string param values
    int m_id;                            ///< Unique ID for the instance
    bool m_writes_globals;               ///< Do I have side effects?
    bool m_userdata_params;              ///< Might I read userdata for params?
    bool m_outgoing_connections;         ///< Any outgoing connections?
    bool m_renderer_outputs;             ///< Any outputs params render outputs?
    bool m_has_error_op;                 ///< Any error ops in the code?
    bool m_has_trace_op;                 ///< Any trace ops in the code?
    bool m_merged_unused;                ///< Unused because of a merge
    bool m_last_layer;                   ///< Is it the group's last layer?
    bool m_entry_layer;                  ///< Is it an entry layer?
    ConnectionVec m_connections;         ///< Connected input params
    int m_firstparam, m_lastparam;       ///< Subset of symbols that are params
    int m_maincodebegin, m_maincodeend;  ///< Main shader code range
    int m_Psym, m_Nsym;                  ///< Quick lookups of common syms

    friend class ShadingSystemImpl;
    friend class RuntimeOptimizer;
    friend class OSL::ShaderGroup;
};



template<int BlockSize> class SimplePool {
public:
    SimplePool()
    {
        // pool must have at least one block available to avoid special cases
        m_blocks.emplace_back(new char[BlockSize]);
        m_block_offset  = BlockSize;
        m_current_block = 0;
    }

    // avoid 'attempting to reference a deleted function' of std::unique_ptr<char>s
    // in reference to those member variables of ShadingContext
    SimplePool(const SimplePool&)            = delete;
    SimplePool(SimplePool&&)                 = delete;
    SimplePool& operator=(const SimplePool&) = delete;
    SimplePool&& operator=(SimplePool&&)     = delete;

    ~SimplePool() {}

    char* alloc(size_t size, size_t alignment = 1)
    {
        // Alignment must be power of two
        OSL_DASSERT((alignment & (alignment - 1)) == 0);

        // Assume sizes are never larger than the configured BlockSize
        OSL_DASSERT(size + alignment - 1 <= BlockSize);

        // Fix up alignment
        m_block_offset += alignment_offset_calc(m_blocks[m_current_block].get()
                                                    + m_block_offset,
                                                alignment);

        // Do we have at least 'size' bytes available in our current block?
        if (m_block_offset + size > BlockSize) {
            // the current block doesn't have enough room, make a new block
            m_current_block++;
            if (m_blocks.size() == m_current_block)
                m_blocks.emplace_back(new char[BlockSize]);
            m_block_offset
                = alignment_offset_calc(m_blocks[m_current_block].get(),
                                        alignment);
        }
        char* ptr = m_blocks[m_current_block].get() + m_block_offset;
        OSL_DASSERT(reinterpret_cast<uintptr_t>(ptr) % alignment == 0);
        m_block_offset += size;
        return ptr;
    }

    void clear()
    {
        m_current_block = 0;
        m_block_offset  = 0;
    }

private:
    static inline size_t alignment_offset_calc(void* ptr, size_t alignment)
    {
        uintptr_t ptrbits = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t offset  = OIIO::round_to_multiple(ptrbits, alignment)
                           - ptrbits;
        OSL_DASSERT((ptrbits + offset) % alignment == 0);
        return offset;
    }

    std::vector<std::unique_ptr<char[]>>
        m_blocks;            ///< Hold blocks of BlockSize bytes
    size_t m_current_block;  ///< Index into the m_blocks array
    size_t m_block_offset;   ///< Offset from the start of the current block
};

/// Represents a single message for use by getmessage and setmessage opcodes
///
struct Message {
    Message(ustringhash name, const TypeDesc& type, int layeridx,
            ustringhash sourcefile, int sourceline, Message* next)
        : name(name)
        , data(nullptr)
        , type(type)
        , layeridx(layeridx)
        , sourcefile(sourcefile)
        , sourceline(sourceline)
        , next(next)
    {
    }

    /// Some messages don't have data because getmessage() was called before
    /// setmessage (which is flagged as an error to avoid ambiguities caused
    /// by execution order)
    bool has_data() const { return data != nullptr; }

    ustringhash name;  ///< name of this message
    char* data;  ///< actual data of the message (will never change once the message is created)
    TypeDesc type;           ///< what kind of data is stored here?
    int layeridx;            ///< layer index where this was message was created
    ustringhash sourcefile;  ///< location of the call that created this message
    int sourceline;          ///< location of the call that created this message
    Message* next;  ///< linked list of messages (managed by MessageList below)
};


/// Represents the list of messages set by a given shader using setmessage and
/// getmessage.
struct MessageList {
    MessageList() : list_head(nullptr), message_data() {}

    void clear()
    {
        list_head = nullptr;
        message_data.clear();
    }

    const Message* find(ustringhash name) const
    {
        for (const Message* m = list_head; m; m = m->next)
            if (m->name == name)
                return m;  // name matches
        return nullptr;    // not found
    }

    void add(ustringhash name, void* data, const TypeDesc& type, int layeridx,
             ustringhash sourcefile, int sourceline)
    {
        list_head = new (message_data.alloc(sizeof(Message), alignof(Message)))
            Message(name, type, layeridx, sourcefile, sourceline, list_head);
        if (data) {
            list_head->data = message_data.alloc(type.size());
            memcpy(list_head->data, data, type.size());
        }
    }

private:
    Message* list_head;
    SimplePool<1024> message_data;
};


#if OSL_USE_BATCHED

struct BatchedMessageBuffer {
    BatchedMessageBuffer() : list_head(nullptr), message_data() {}
    BatchedMessageBuffer(const BatchedMessageBuffer&)            = delete;
    BatchedMessageBuffer& operator=(const BatchedMessageBuffer&) = delete;

    void clear()
    {
        list_head = NULL;
        message_data.clear();
    }

    void* list_head;
    SimplePool<16 * 1024> message_data;
};


#endif

};  // namespace pvt



/// A ShaderGroup consists of one or more layers (each of which is a
/// ShaderInstance), and the connections among them.
class ShaderGroup {
public:
    ShaderGroup(string_view name, ShadingSystemImpl& shadingsys);
    ~ShaderGroup();

    /// Clear the layers
    ///
    void clear()
    {
        m_layers.clear();
        m_optimized  = 0;
        m_jitted     = 0;
        m_executions = 0;
    }

    /// Append a new shader instance on to the end of this group
    ///
    void append(ShaderInstanceRef newlayer)
    {
        OSL_ASSERT(!m_optimized && "should not append to optimized group");
        m_layers.push_back(newlayer);
    }

    /// How many layers are in this group?
    ///
    int nlayers() const { return (int)m_layers.size(); }

    ShaderInstance* layer(int i) const { return m_layers[i].get(); }

    /// Array indexing returns the i-th layer of the group
    ShaderInstance* operator[](int i) const { return layer(i); }

    /// Return a reference to the shading system for this group.
    ShadingSystemImpl& shadingsys() const { return m_shadingsys; }

    int optimized() const { return m_optimized; }
    void optimized(int opt) { m_optimized = opt; }

    int jitted() const { return m_jitted; }
    void jitted(int jitted) { m_jitted = jitted; }

    int batch_jitted() const { return m_batch_jitted; }
    void batch_jitted(int batch_jitted) { m_batch_jitted = batch_jitted; }

    size_t llvm_groupdata_size() const { return m_llvm_groupdata_size; }
    void llvm_groupdata_size(size_t size) { m_llvm_groupdata_size = size; }

    size_t llvm_groupdata_wide_size() const
    {
        return m_llvm_groupdata_wide_size;
    }
    void llvm_groupdata_wide_size(size_t size)
    {
        m_llvm_groupdata_wide_size = size;
    }

    RunLLVMGroupFunc llvm_compiled_version() const
    {
        return m_llvm_compiled_version;
    }
    void llvm_compiled_version(RunLLVMGroupFunc func)
    {
        m_llvm_compiled_version = func;
    }
    RunLLVMGroupFunc llvm_compiled_init() const { return m_llvm_compiled_init; }
    void llvm_compiled_init(RunLLVMGroupFunc func)
    {
        m_llvm_compiled_init = func;
    }
    RunLLVMGroupFunc llvm_compiled_layer(int layer) const
    {
        return layer < (int)m_llvm_compiled_layers.size()
                   ? m_llvm_compiled_layers[layer]
                   : NULL;
    }
    void llvm_compiled_layer(int layer, RunLLVMGroupFunc func)
    {
        m_llvm_compiled_layers.resize((size_t)nlayers(), NULL);
        if (layer < nlayers())
            m_llvm_compiled_layers[layer] = func;
    }

#if OSL_USE_BATCHED
    // Hold onto wide versions of llvm functions side by side with scalar
    RunLLVMGroupFuncWide llvm_compiled_wide_version() const
    {
        return m_llvm_compiled_wide_version;
    }
    void llvm_compiled_wide_version(RunLLVMGroupFuncWide func)
    {
        m_llvm_compiled_wide_version = func;
    }
    RunLLVMGroupFuncWide llvm_compiled_wide_init() const
    {
        return m_llvm_compiled_wide_init;
    }
    void llvm_compiled_wide_init(RunLLVMGroupFuncWide func)
    {
        m_llvm_compiled_wide_init = func;
    }
    RunLLVMGroupFuncWide llvm_compiled_wide_layer(int layer) const
    {
        return layer < (int)m_llvm_compiled_wide_layers.size()
                   ? m_llvm_compiled_wide_layers[layer]
                   : NULL;
    }
    void llvm_compiled_wide_layer(int layer, RunLLVMGroupFuncWide func)
    {
        m_llvm_compiled_wide_layers.resize((size_t)nlayers(), NULL);
        if (layer < nlayers())
            m_llvm_compiled_wide_layers[layer] = func;
    }
#endif
    // Is this shader group equivalent to ret void?
    bool does_nothing() const { return m_does_nothing; }
    void does_nothing(bool new_val) { m_does_nothing = new_val; }

    long long int executions() const { return m_executions; }

    void start_running()
    {
#ifndef NDEBUG
        m_executions++;
#endif
    }

    void name(ustring name) { m_name = name; }
    ustring name() const { return m_name; }

    // Generate and memoize the cache key so we don't calculate it twice
    void generate_optix_cache_key(string_view code);
    std::string optix_cache_key() const { return m_optix_cache_key; }

    std::string serialize() const;

    void lock() const { m_mutex.lock(); }
    void unlock() const { m_mutex.unlock(); }

    // Find which layer index corresponds to the layer name. Return -1 if
    // not found.
    int find_layer(ustring layername) const;

    // Retrieve the Symbol* by layer and symbol name. If the layer name is
    // empty, go back-to-front.
    const Symbol* find_symbol(ustring layername, ustring symbolname) const;

    /// Return a unique ID of this group.
    ///
    int id() const { return m_id; }

    /// Mark all layers as not entry points and set m_num_entry_layers to 0.
    void clear_entry_layers();

    /// Mark the given layer number as an entry layer and increment
    /// m_num_entry_layers if it wasn't already set. Has no effect if layer
    /// is not a valid layer ID.
    void mark_entry_layer(int layer);
    void mark_entry_layer(ustring layername)
    {
        mark_entry_layer(find_layer(layername));
    }

    int num_entry_layers() const { return m_num_entry_layers; }

    bool is_last_layer(int layer) const { return layer == nlayers() - 1; }

    /// Is the given layer an entry point? It is if explicitly tagged as
    /// such, or if no layers are so tagged then the last layer is the one
    /// entry.
    bool is_entry_layer(int layer) const
    {
        return num_entry_layers() ? m_layers[layer]->entry_layer()
                                  : is_last_layer(layer);
    }

    int raytype_queries() const { return m_raytype_queries; }

    /// Optionally set which ray types are known to be on or off (0 means
    /// not known at optimize time).
    void set_raytypes(int raytypes_on, int raytypes_off)
    {
        m_raytypes_on  = raytypes_on;
        m_raytypes_off = raytypes_off;
    }
    int raytypes_on() const { return m_raytypes_on; }
    int raytypes_off() const { return m_raytypes_off; }

    void clear_symlocs() { m_symlocs.clear(); }
    void add_symlocs(cspan<SymLocationDesc> symlocs)
    {
        for (auto& s : symlocs) {
            // Insert and maintain sorted order
            auto f = std::lower_bound(m_symlocs.begin(), m_symlocs.end(),
                                      s.name);
            if (f == m_symlocs.end())  // Greater than anything in the list
                m_symlocs.emplace_back(s);
            else if (f->name == s.name)  // Already in list -- replace it
                *f = s;
            else                          // f points to the first symbol > name
                m_symlocs.emplace(f, s);  // Insert into the right position
        }
    }
    // Find the SymLocationDesc for this named param, returning its pointer
    // or nullptr if that name is not found.
    const SymLocationDesc* find_symloc(ustring name) const
    {
        auto f = std::lower_bound(m_symlocs.begin(), m_symlocs.end(), name);
        return (f == m_symlocs.end() || f->name != name) ? nullptr : &(*f);
    }

    // Find the SymLocationDesc for this named param but only if it matches
    // the arena type, returning its pointer or nullptr if that name is not
    // found.
    const SymLocationDesc* find_symloc(ustring name, SymArena arena) const
    {
        auto f = std::lower_bound(m_symlocs.begin(), m_symlocs.end(), name);
        if (f != m_symlocs.end() && f->name == name && f->arena == arena
            && f->offset != -1)
            return &(*f);
        else
            return nullptr;
    }

    // Find the SymLocationDesc for this named param but only if it matches
    // the arena type, returning its pointer or nullptr if that name is not
    // found.
    // Try to look up the sym with the full layer.name specification first.
    // If that fails, try again based on name only.
    const SymLocationDesc* find_symloc(ustring name, ustring layer,
                                       SymArena arena) const
    {
        ustring layersym = ustring::fmtformat("{}.{}", layer, name);
        auto symloc      = find_symloc(layersym, arena);
        if (!symloc)
            symloc = find_symloc(name, arena);
        return symloc;
    }

    // Given a data block for interactive params, allocate space for it to
    // live with the group and copy the initial data.
    void setup_interactive_arena(cspan<uint8_t> paramblock);

    uint8_t* interactive_arena_ptr() { return m_interactive_arena.get(); }

    device_ptr<uint8_t>& device_interactive_arena()
    {
        return m_device_interactive_arena;
    }

    struct InteractiveParamData {
        int layer;
        ustring name;
        int offset;

        InteractiveParamData(int layer, ustring name, int offset)
            : layer(layer), name(name), offset(offset)
        {
        }
        bool operator==(const InteractiveParamData& other) const
        {
            return layer == other.layer && name == other.name;
        }
        bool operator<(const InteractiveParamData& other) const
        {
            return layer < other.layer
                   || (layer == other.layer && name < other.name);
        }
    };

    void add_interactive_param(int layer, ustring name, size_t offset)
    {
        m_interactive_params.emplace_back(layer, name,
                                          static_cast<int>(offset));
    }

    int interactive_param_offset(int layer, ustring name)
    {
        for (auto& f : m_interactive_params)
            if (f.layer == layer && f.name == name)
                return f.offset;
        return -1;
    }

private:
    // Put all the things that are read-only (after optimization) and
    // needed on every shade execution at the front of the struct, as much
    // together on one cache line as possible.
    volatile int m_optimized = 0;  ///< Is it already optimized?
    volatile int m_jitted    = 0;  ///< Is it already jitted?
    bool m_does_nothing
        = false;  ///< Is the shading group just func() { return; }
    volatile int m_batch_jitted
        = 0;  ///< Is it already jitted for batch execution?
    size_t m_llvm_groupdata_size = 0;  ///< Heap size needed for its groupdata
    size_t m_llvm_groupdata_wide_size
        = 0;                     ///< Heap size needed for its wide groupdata
    int m_id;                    ///< Unique ID for the group
    int m_num_entry_layers = 0;  ///< Number of marked entry layers
    RunLLVMGroupFunc m_llvm_compiled_version = nullptr;
    RunLLVMGroupFunc m_llvm_compiled_init    = nullptr;
    std::vector<RunLLVMGroupFunc> m_llvm_compiled_layers;
#if OSL_USE_BATCHED
    RunLLVMGroupFuncWide m_llvm_compiled_wide_version = nullptr;
    RunLLVMGroupFuncWide m_llvm_compiled_wide_init    = nullptr;
    std::vector<RunLLVMGroupFuncWide> m_llvm_compiled_wide_layers;
#endif
    std::vector<ShaderInstanceRef> m_layers;
    ustring m_name;
    int m_exec_repeat     = 1;   ///< How many times to execute group
    int m_raytype_queries = -1;  ///< Bitmask of raytypes queried
    int m_raytypes_on     = 0;   ///< Bitmask of raytypes we assume to be on
    int m_raytypes_off    = 0;   ///< Bitmask of raytypes we assume to be off
    mutable mutex m_mutex;       ///< Thread-safe optimization
    int m_globals_read  = 0;
    int m_globals_write = 0;
    std::vector<ustring> m_textures_needed;
    std::vector<ustring> m_closures_needed;
    std::vector<ustring> m_globals_needed;  // semi-deprecated
    std::vector<ustring> m_userdata_names;
    std::vector<TypeDesc> m_userdata_types;
    std::vector<int> m_userdata_offsets;
    std::vector<char> m_userdata_derivs;
    std::vector<int> m_userdata_layers;
    std::vector<void*> m_userdata_init_vals;
    std::vector<ustring> m_attributes_needed;
    std::vector<ustring> m_attribute_scopes;
    std::vector<TypeDesc> m_attribute_types;
    std::vector<char> m_attribute_derivs;
    std::vector<ustring> m_renderer_outputs;  ///< Names of renderer outputs
    std::vector<SymLocationDesc> m_symlocs;   ///< SORTED!!
    bool m_unknown_textures_needed;
    bool m_unknown_closures_needed;
    bool m_unknown_attributes_needed;
    atomic_ll m_executions { 0 };  ///< Number of times the group executed
    atomic_ll m_stat_total_shading_time_ticks { 0 };  // Shading time (ticks)

    std::string m_optix_cache_key;

    // PTX assembly for compiled ShaderGroup
    std::string m_llvm_ptx_compiled_version;

    ParamValueList m_pending_params;          // Pending Parameter() values
    std::vector<ParamHints> m_pending_hints;  // ParamHints of pending params
    ustring m_group_use;                      // "Usage" of group
    bool m_complete = false;                  // Successfully ShaderGroupEnd?

    ShadingSystemImpl& m_shadingsys;  // Back-ptr to the shading system

    // Per-group home for interactively editable parameters
    std::vector<InteractiveParamData> m_interactive_params;
    std::unique_ptr<uint8_t[]> m_interactive_arena;
    size_t m_interactive_arena_size = 0;
    device_ptr<uint8_t> m_device_interactive_arena;

    friend class OSL::pvt::ShadingSystemImpl;
    friend class OSL::pvt::BackendLLVM;
#if OSL_USE_BATCHED
    friend class OSL::pvt::BatchedBackendLLVM;
#endif
    friend class ShadingContext;
};



/// The full context for executing a shader group.
///
class OSLEXECPUBLIC ShadingContext {
public:
    ShadingContext(ShadingSystemImpl& shadingsys, PerThreadInfo* threadinfo);
    ~ShadingContext();

    /// Return a reference to the shading system for this context.
    ///
    ShadingSystemImpl& shadingsys() const { return m_shadingsys; }

    /// Get a pointer to the RendererServices for this execution.
    ///
    RendererServices* renderer() const { return m_renderer; }

    /// Bind a shader group and globals to this context and prepare to
    /// execute. (See similarly named method of ShadingSystem.)
    bool execute_init(ShaderGroup& group, int threadindex, int shadeindex,
                      ShaderGlobals& globals, void* userdata_base_ptr,
                      void* output_base_ptr, bool run);

    /// Execute the layer whose index is specified. (See similarly named
    /// method of ShadingSystem.)
    bool execute_layer(int threadindex, int shadeindex, ShaderGlobals& globals,
                       void* userdata_base_ptr, void* output_base_ptr,
                       int layer);

    /// Signify that this context is done with the current execution of the
    /// group. (See similarly named method of ShadingSystem.)
    bool execute_cleanup();

    /// Execute the shader group, including init, run of single entry point
    /// layer, and cleanup. (See similarly named method of ShadingSystem.)
    bool execute(ShaderGroup& group, int threadindex, int shadeindex,
                 ShaderGlobals& globals, void* userdata_base_ptr,
                 void* output_base_ptr, bool run);

#if OSL_USE_BATCHED
    // Group all batched methods behind a templated interface
    // so we can support multiple widths
    template<int WidthT> class OSLEXECPUBLIC Batched {
        ShadingContext& m_sc;
        OSL_FORCEINLINE ShadingContext& context() { return m_sc; }

    public:
        explicit OSL_FORCEINLINE Batched(ShadingContext& sc) : m_sc(sc) {}
        OSL_FORCEINLINE Batched(const Batched&) = default;

        OSL_FORCEINLINE ShadingSystemImpl& shadingsys()
        {
            return context().m_shadingsys;
        }
        OSL_FORCEINLINE ShaderGroup* group() { return context().m_group; }
        OSL_FORCEINLINE BatchedRendererServices<WidthT>* renderer()
        {
            // turn templated call to polymorphic virtual function call
            return context().m_renderer->batched(WidthOf<WidthT>());
        }

        /// Bind a shader group and batched of globals to this context and prepare to
        /// execute. (See similarly named method of ShadingSystem.)
        bool execute_init(ShaderGroup& group, int batch_size,
                          Wide<const int, WidthT> wide_shadeindex,
                          BatchedShaderGlobals<WidthT>& bsg,
                          void* userdata_base_ptr, void* output_base_ptr,
                          bool run = true);

        /// Execute the layer whose index is specified. (See similarly named
        /// method of ShadingSystem.)
        bool execute_layer(int batch_size,
                           Wide<const int, WidthT> wide_shadeindex,
                           BatchedShaderGlobals<WidthT>& bsg,
                           void* userdata_base_ptr, void* output_base_ptr,
                           int layer);

        /// Execute the shader group, including init, run of single entry point
        /// layer, and cleanup. (See similarly named method of ShadingSystem.)
        bool execute(ShaderGroup& group, int batch_size,
                     Wide<const int, WidthT> wide_shadeindex,
                     BatchedShaderGlobals<WidthT>& bsg, void* userdata_base_ptr,
                     void* output_base_ptr, bool run = true);

        ClosureComponent* closure_component_allot(size_t prim_size)
        {
            // Allocate the component and the mul back to back
            constexpr int alignment = alignof(ClosureComponent);
            size_t stride
                = (int)((sizeof(ClosureComponent) + prim_size + alignment - 1)
                        / alignment)
                  * alignment;
            size_t needed = WidthT * stride;
            ClosureComponent* comp_mem
                = (ClosureComponent*)m_sc.m_closure_pool.alloc(needed,
                                                               alignment);
            return comp_mem;
        }

        ClosureMul* closure_mul_allot()
        {
            return (ClosureMul*)m_sc.m_closure_pool.alloc(
                WidthT * sizeof(ClosureMul), alignof(ClosureMul));
        }

        ClosureAdd* closure_add_allot()
        {
            return (ClosureAdd*)m_sc.m_closure_pool.alloc(
                WidthT * sizeof(ClosureAdd), alignof(ClosureAdd));
        }

        template<typename Str, typename... Args>
        inline void errorfmt(Mask<WidthT> mask, const Str& fmt,
                             Args&&... args) const
        {
            m_sc.record_error(ErrorHandler::EH_ERROR,
                              fmtformat(fmt, std::forward<Args>(args)...),
                              static_cast<Mask<MaxSupportedSimdLaneCount>>(
                                  mask));
        }

        template<typename Str, typename... Args>
        inline void warningfmt(Mask<WidthT> mask, const Str& fmt,
                               Args&&... args) const
        {
            m_sc.record_error(ErrorHandler::EH_WARNING,
                              fmtformat(fmt, std::forward<Args>(args)...),
                              static_cast<Mask<MaxSupportedSimdLaneCount>>(
                                  mask));
        }

        template<typename Str, typename... Args>
        inline void infofmt(Mask<WidthT> mask, const Str& fmt,
                            Args&&... args) const
        {
            m_sc.record_error(ErrorHandler::EH_INFO,
                              fmtformat(fmt, std::forward<Args>(args)...),
                              static_cast<Mask<MaxSupportedSimdLaneCount>>(
                                  mask));
        }

        template<typename Str, typename... Args>
        inline void messagefmt(Mask<WidthT> mask, const Str& fmt,
                               Args&&... args) const
        {
            m_sc.record_error(ErrorHandler::EH_MESSAGE,
                              fmtformat(fmt, std::forward<Args>(args)...),
                              static_cast<Mask<MaxSupportedSimdLaneCount>>(
                                  mask));
        }
    };

    template<int WidthT> OSL_FORCEINLINE Batched<WidthT> batched()
    {
        return Batched<WidthT>(*this);
    }
#endif

    ClosureComponent* closure_component_allot(int id, size_t prim_size,
                                              const Color3& w)
    {
        // Allocate the component and the mul back to back
        size_t needed          = sizeof(ClosureComponent) + prim_size;
        ClosureComponent* comp = (ClosureComponent*)m_closure_pool.alloc(
            needed, alignof(ClosureComponent));
        comp->id = id;
        comp->w  = w;
        return comp;
    }

    ClosureMul* closure_mul_allot(const Color3& w, const ClosureColor* c)
    {
        ClosureMul* mul = (ClosureMul*)m_closure_pool.alloc(sizeof(ClosureMul),
                                                            alignof(ClosureMul));
        mul->id         = ClosureColor::MUL;
        mul->weight     = w;
        mul->closure    = c;
        return mul;
    }

    ClosureMul* closure_mul_allot(float w, const ClosureColor* c)
    {
        ClosureMul* mul = (ClosureMul*)m_closure_pool.alloc(sizeof(ClosureMul),
                                                            alignof(ClosureMul));
        mul->id         = ClosureColor::MUL;
        mul->weight.setValue(w, w, w);
        mul->closure = c;
        return mul;
    }

    ClosureAdd* closure_add_allot(const ClosureColor* a, const ClosureColor* b)
    {
        ClosureAdd* add = (ClosureAdd*)m_closure_pool.alloc(sizeof(ClosureAdd),
                                                            alignof(ClosureAdd));
        add->id         = ClosureColor::ADD;
        add->closureA   = a;
        add->closureB   = b;
        return add;
    }


    /// Find the named symbol in the (already-executed!) stack of shaders of
    /// the given use. If a layer is given, search just that layer. If no
    /// layer is specified, priority is given to later laters over earlier
    /// layers (if they name the same symbol). Return NULL if no such symbol
    /// is found.
    const Symbol* symbol(ustring layername, ustring symbolname) const;
    const Symbol* symbol(ustring symbolname) const
    {
        return symbol(ustring(), symbolname);
    }

    /// Return a pointer to where the symbol's data lives.
    const void* symbol_data(const Symbol& sym) const;

    /// Return a reference to a compiled regular expression for the
    /// given string, being careful to cache already-created ones so we
    /// aren't constantly compiling new ones.
    const std::regex& find_regex(ustring r);

    /// Return a pointer to the shading group for this context.
    ///
    ShaderGroup* group() { return m_group; }
    const ShaderGroup* group() const { return m_group; }
    void group(ShaderGroup* grp) { m_group = grp; }

    /// Return a reference to the MessageList containing messages.
    ///
    MessageList& messages() { return m_messages; }
#if OSL_USE_BATCHED
    BatchedMessageBuffer& batched_messages_buffer()
    {
        return m_batched_messages_buffer;
    }
#endif

    /// Look up a query from a dictionary (typically XML), staring the
    /// search from the root of the dictionary, and returning ID of the
    /// first matching node.
    int dict_find(ExecContextPtr ec, ustring dictionaryname, ustring query);
    /// Look up a query from a dictionary (typically XML), staring the
    /// search from the given nodeID within the dictionary, and
    /// returning ID of the first matching node.
    int dict_find(ExecContextPtr ec, int nodeID, ustring query);
    /// Return the next match of the same query that gave the nodeID.
    int dict_next(int nodeID);
    /// Look up an attribute of the given dictionary node.  If
    /// attribname is "", return the value of the node itself.
    int dict_value(int nodeID, ustring attribname, TypeDesc type, void* data,
                   bool treat_ustrings_as_hash);

    bool osl_get_attribute(ShaderGlobals* sg, void* objdata, int dest_derivs,
                           ustringhash obj_name, ustringhash attr_name,
                           int array_lookup, int index, TypeDesc attr_type,
                           void* attr_dest);

    PerThreadInfo* thread_info() const { return m_threadinfo; }

    TextureSystem::Perthread* texture_thread_info() const
    {
        if (!m_texture_thread_info)
            m_texture_thread_info
                = shadingsys().texturesys()->get_perthread_info();
        return m_texture_thread_info;
    }

    void texture_thread_info(TextureSystem::Perthread* t)
    {
        m_texture_thread_info = t;
    }

    const LLVM_Util::PerThreadInfo& llvm_thread_info() const
    {
        return thread_info()->llvm_thread_info;
    }

    void* alloc_scratch(size_t size, size_t align = 1)
    {
        return m_scratch_pool.alloc(size, align);
    }

    template<typename Color>
    bool ocio_transform(ustring fromspace, ustring tospace, const Color& C,
                        Color& Cout);

    void incr_layers_executed() { ++m_stat_layers_executed; }

    void incr_get_userdata_calls() { ++m_stat_get_userdata_calls; }

    // Clear the stats we record per-execution in this context (unlocked)
    void clear_runtime_stats()
    {
        m_stat_get_userdata_calls = 0;
        m_stat_layers_executed    = 0;
    }

    // Transfer the per-execution stats from this context to the shading
    // system.
    void record_runtime_stats()
    {
        shadingsys().m_stat_get_userdata_calls += m_stat_get_userdata_calls;
        shadingsys().m_stat_layers_executed += m_stat_layers_executed;
    }

    bool allow_warnings()
    {
        if (m_max_warnings > 0) {
            // at least one more to go
            m_max_warnings--;
            return true;
        } else {
            // we've processed enough with this context
            return false;
        }
    }

    // Record an error (or warning, printf, etc.)
    void record_error(ErrorHandler::ErrCode code,
                      const std::string& text) const;
#if OSL_USE_BATCHED
    void record_error(ErrorHandler::ErrCode code, const std::string& text,
                      Mask<MaxSupportedSimdLaneCount> mask) const;
    void record_to_file(ustring filename, const std::string& text,
                        Mask<MaxSupportedSimdLaneCount> mask) const;
    // Process all the recorded fprintf messages
    void process_file_output() const;
#endif
    // Process all the recorded errors, warnings, printfs
    void process_errors() const;

    template<typename... Args>
    inline void errorfmt(const char* fmt, const Args&... args) const
    {
        record_error(ErrorHandler::EH_ERROR, fmtformat(fmt, args...));
    }

    template<typename... Args>
    inline void warningfmt(const char* fmt, const Args&... args) const
    {
        record_error(ErrorHandler::EH_WARNING, fmtformat(fmt, args...));
    }

    template<typename... Args>
    inline void infofmt(const char* fmt, const Args&... args) const
    {
        record_error(ErrorHandler::EH_INFO, fmtformat(fmt, args...));
    }

    template<typename... Args>
    inline void messagefmt(const char* fmt, const Args&... args) const
    {
        record_error(ErrorHandler::EH_MESSAGE, fmtformat(fmt, args...));
    }

    void reserve_heap(size_t size)
    {
        if (size > m_heapsize) {
            m_heap.reset(
                (char*)OIIO::aligned_malloc(size, OIIO_CACHE_LINE_SIZE));
            m_heapsize = size;
        }
    }

private:
    void free_dict_resources();

    ShadingSystemImpl& m_shadingsys;  ///< Backpointer to shadingsys
    RendererServices* m_renderer;     ///< Ptr to renderer services
    PerThreadInfo* m_threadinfo;      ///< Ptr to our thread's info
    mutable TextureSystem::Perthread*
        m_texture_thread_info;  ///< Ptr to texture thread info
    ShaderGroup* m_group;       ///< Ptr to shader group
    // Heap memory
    std::unique_ptr<char, decltype(&OIIO::aligned_free)> m_heap {
        nullptr, &OIIO::aligned_free
    };
    size_t m_heapsize = 0;
    using RegexMap = std::unordered_map<ustring, std::unique_ptr<std::regex>>;
    RegexMap m_regex_map;    ///< Compiled regex's
    MessageList m_messages;  ///< Message blackboard
#if OSL_USE_BATCHED
    BatchedMessageBuffer
        m_batched_messages_buffer;  ///< Buffer for Batched Message blackboard
#endif
    int m_max_warnings;             ///< To avoid processing too many warnings
    int m_stat_get_userdata_calls;  ///< Number of calls to get_userdata
    int m_stat_layers_executed;     ///< Number of layers executed
    long long m_ticks;              ///< Time executing the shader

    SimplePool<20 * 1024> m_closure_pool;
    SimplePool<64 * 1024> m_scratch_pool;

    Dictionary* m_dictionary;

    OCIOColorSystem m_ocio_system;

    // Buffering of error messages and printfs
    struct ErrorItem {
        ErrorItem() = default;
        ErrorItem(ErrorHandler::ErrCode err_code_, std::string msgString_,
                  Mask<MaxSupportedSimdLaneCount> mask_
                  = Mask<MaxSupportedSimdLaneCount>(true))
            : err_code(err_code_), msgString(msgString_), mask(mask_)
        {
        }

        ErrorHandler::ErrCode err_code;
        std::string msgString;
        Mask<MaxSupportedSimdLaneCount> mask;
    };
    mutable std::vector<ErrorItem> m_buffered_errors;

#if OSL_USE_BATCHED
    // Buffering of fprintf's so they can be output
    // to the file one data lane at a time
    struct FileItem {
        FileItem() = default;
        FileItem(ustring filename_, std::string msgString_,
                 Mask<MaxSupportedSimdLaneCount> mask_)
            : filename(filename_), msgString(msgString_), mask(mask_)
        {
        }

        ustring filename;
        std::string msgString;
        Mask<MaxSupportedSimdLaneCount> mask;
    };
    mutable std::vector<FileItem> m_buffered_file_output;

#endif
    // When interpreting symbol addresses we need to know if the
    // wide data offsets should be used
    int batch_size_executed;
    bool execution_is_batched() const { return batch_size_executed != 0; }


    struct RestoreState {
        TextureSystem::Perthread* m_pre_jit_texture_thread_info;
    };

    // Rather than allocate an additional ShadingContext for JIT
    // reuse this one by processing any existing errors
    // and saving off any necessary state to be restored afterwards
    RestoreState repurposeForJit();
    // Process any errors from JIT and restore the state
    void restoreFromJit(const RestoreState&);
};


OSL_HOSTDEVICE inline int
tex_interp_to_code(ustringhash modename)
{
    int mode = -1;
    if (modename == Hashes::smartcubic)
        mode = (int)TextureOpt::InterpSmartBicubic;
    else if (modename == Hashes::linear)
        mode = (int)TextureOpt::InterpBilinear;
    else if (modename == Hashes::cubic)
        mode = (int)TextureOpt::InterpBicubic;
    else if (modename == Hashes::closest)
        mode = (int)TextureOpt::InterpClosest;
    return mode;
}

#ifndef __CUDACC__
inline int
tex_interp_to_code(ustring modename)
{
    int mode = -1;
    if (modename == Strings::smartcubic)
        mode = (int)TextureOpt::InterpSmartBicubic;
    else if (modename == Strings::linear)
        mode = (int)TextureOpt::InterpBilinear;
    else if (modename == Strings::cubic)
        mode = (int)TextureOpt::InterpBicubic;
    else if (modename == Strings::closest)
        mode = (int)TextureOpt::InterpClosest;
    return mode;
}
#endif



// Layout of structure we use to pass noise parameters
struct NoiseParams {
    int anisotropic;
    int do_filter;
    Vec3 direction;
    float bandwidth;
    float impulses;

    OSL_HOSTDEVICE NoiseParams()
        : anisotropic(0)
        , do_filter(true)
        , direction(1.0f, 0.0f, 0.0f)
        , bandwidth(1.0f)
        , impulses(16.0f)
    {
    }
};



namespace pvt {

// Mangle the group and layer into a unique function name
std::string
layer_function_name(const ShaderGroup& group, const ShaderInstance& inst,
                    bool api = false);

std::string
init_function_name(const ShadingSystemImpl& shadingsys,
                   const ShaderGroup& group, bool api = false);

std::string
fused_function_name(const ShaderGroup& group);

/// Base class for objects that examine compiled shader groups (oso).
/// This includes optimization passes, "back end" code generators, etc.
/// The base class holds common data structures and methods that all
/// such processors will need.
class OSOProcessorBase {
public:
    OSOProcessorBase(ShadingSystemImpl& shadingsys, ShaderGroup& group,
                     ShadingContext* context);

    virtual ~OSOProcessorBase();

    /// Do its thing.
    virtual void run() {}

    /// Return a reference to the shader group being optimized.
    ShaderGroup& group() const { return m_group; }

    /// Return a reference to the shading system.
    ShadingSystemImpl& shadingsys() const { return m_shadingsys; }

    /// Return a reference to the texture system.
    TextureSystem* texturesys() const { return shadingsys().texturesys(); }

    /// Return a reference to the RendererServices.
    RendererServices* renderer() const { return shadingsys().renderer(); }

    /// Retrieve the dummy shading context.
    ShadingContext* shadingcontext() const { return m_context; }

    /// Re-set what debugging level we ought to be at.
    virtual void set_debug();

    /// What debug level are we at?
    int debug() const { return m_debug; }

    /// Set which instance (layer within the group) we are currently
    /// examining.  This lets you walk through the layers in turn.
    virtual void set_inst(int layer);

    /// Return the layer number that we currently examining.
    int layer() const { return m_layer; }

    /// Return a pointer to the currently-examining instance within the
    /// group.
    ShaderInstance* inst() const { return m_inst; }

    /// Return a reference to a particular indexed op in the current inst
    Opcode& op(int opnum) { return inst()->ops()[opnum]; }

    /// Return a pointer to a particular indexed symbol in the current inst
    Symbol* symbol(int symnum) { return inst()->symbol(symnum); }

    /// Return the symbol index of the symbol that is the argnum-th argument
    /// to the given op in the current instance.
    int oparg(const Opcode& op, int argnum) const
    {
        return inst()->arg(op.firstarg() + argnum);
    }

    /// Return the ptr to the symbol that is the argnum-th argument to the
    /// given op in the current instance.
    Symbol* opargsym(const Opcode& op, int argnum)
    {
        return (argnum < op.nargs()) ? inst()->argsymbol(op.firstarg() + argnum)
                                     : NULL;
    }

    /// Is the symbol a constant whose value is 0?
    static bool is_zero(const Symbol& A);

    /// Is the symbol a constant whose value is 1?
    static bool is_one(const Symbol& A);

    /// Is the symbol a constant whose value is nonzero in all components?
    static bool is_nonzero(const Symbol& A);

    /// For debugging, express A's constant value as a string.
    static std::string const_value_as_string(const Symbol& A);

    /// Set up m_in_conditional[] to be true for all ops that are inside of
    /// conditionals, false for all unconditionally-executed ops,
    /// m_in_loop[] to be true for all ops that are inside a loop, and
    /// m_first_return to be the op number of the first return/exit
    /// statement (or code.size() if there is no return/exit statement).
    void find_conditionals();

    /// Identify basic blocks by assigning a basic block ID for each
    /// instruction.  Within any basic bock, there are no jumps in or out.
    /// Also note which instructions are inside conditional states.
    void find_basic_blocks();

    /// Will the op executed for-sure unconditionally every time the
    /// shader is run?  (Not inside a loop or conditional or after a
    /// possible early exit from the shader.)
    bool op_is_unconditionally_executed(int opnum) const
    {
        return !m_in_conditional[opnum] && opnum < m_first_return;
    }

    /// Return the basic block ID for the given instruction.
    int bblockid(int opnum) const { return m_bblockids[opnum]; }

protected:
    ShadingSystemImpl& m_shadingsys;  ///< Backpointer to shading system
    ShaderGroup& m_group;             ///< Group we're processing
    ShadingContext* m_context;        ///< Shading context
    int m_debug;                      ///< Current debug level

    // All below is just for the one inst we're optimizing at the moment:
    ShaderInstance* m_inst;              ///< Instance we're optimizing
    int m_layer;                         ///< Layer we're optimizing
    std::vector<int> m_bblockids;        ///< Basic block IDs for each op
    std::vector<char> m_in_conditional;  ///< Whether each op is in a cond
    std::vector<char> m_in_loop;         ///< Whether each op is in a loop
    int m_first_return;                  ///< Op number of first return or exit

    struct CallLayerKey {
        int bblockid;
        int layerid;

        bool operator<(const CallLayerKey& other) const
        {
            return bblockid < other.bblockid
                   || (bblockid == other.bblockid && layerid < other.layerid);
        }
    };
    std::set<CallLayerKey>
        m_call_layers_inserted;  ///< Lookup used during llvm gen
};

};  // namespace pvt


OSL_NAMESPACE_END
