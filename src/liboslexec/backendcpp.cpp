// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage


#include <mutex>

#include "oslexec_pvt.h"

#include "backendcpp.h"

using namespace OSL;
using namespace OSL::pvt;

OSL_NAMESPACE_BEGIN

namespace pvt {


BackendCpp::BackendCpp(ShadingSystemImpl& shadingsys, ShaderGroup& group,
                       ShadingContext* ctx)
    : OSOProcessorBase(shadingsys, group, ctx)
{
    op_gen_init();
}



BackendCpp::~BackendCpp() {}



static std::string indent_reservoir(128, ' ');



void
BackendCpp::indent(int delta)
{
    m_indentlevel += delta;
    m_indentview = string_view(indent_reservoir.c_str(),
                               OIIO::clamp(m_indentlevel, 0,
                                           int(indent_reservoir.size())));
}



std::string
BackendCpp::cpp_typedesc_name(TypeDesc type)
{
    if (type.basetype == TypeDesc::STRING)
        type.basetype = TypeDesc::USTRINGHASH;
    return type.c_str();
}



std::string
BackendCpp::cpp_sym_type_name(const Symbol& sym)
{
    std::string str;
    TypeSpec t = sym.typespec();
    if (t.is_closure() || t.is_closure_array()) {
        if (t.is_unsized_array())
            str = "closure_color_t[]";
        else if (t.arraylength() > 0)
            str = Strutil::fmt::format("closure_color_t[{}]", t.arraylength());
    } else if (t.structure() > 0) {
        StructSpec* ss = t.structspec();
        if (ss)
            str += Strutil::fmt::format("struct {}", t.structspec()->name());
        else
            str += Strutil::fmt::format("struct {}", t.structure());
        if (t.is_unsized_array())
            str += "[]";
        else if (t.arraylength() > 0)
            str += Strutil::fmt::format("[{}]", t.arraylength());
    } else {
        str = cpp_typedesc_name(t.simpletype());
    }
    return str;
}



std::string
BackendCpp::cpp_var_declaration(const Symbol& sym)
{
    std::string decl;
    if (sym.symtype() == SymTypeConst) {
        decl = fmtformat("const {} {}", cpp_sym_type_name(sym),
                         sym.cpp_safe_name());
    } else if (sym.symtype() == SymTypeParam
               || sym.symtype() == SymTypeOutputParam) {
        decl = fmtformat("{} {} /* = init TBD */", cpp_sym_type_name(sym),
                         sym.cpp_safe_name());
    } else if (sym.symtype() == SymTypeTemp || sym.symtype() == SymTypeLocal) {
        decl = fmtformat("{} {}", cpp_sym_type_name(sym), sym.cpp_safe_name());
    }
    return decl;
}



void
BackendCpp::run()
{
    int nlayers = (int)group().nlayers();
    for (int layer = 0; layer < nlayers; ++layer) {
        set_inst(layer);
        if (inst()->unused())
            continue;  // no need to print or gather stats for unused layers
        find_basic_blocks();

        outputfmt("// Layer {}: {}\n", layer, inst()->layername());

        outputfmt("// Shader {}{}\n", inst()->shadername(),
                  inst()->unused() ? " (UNUSED)" : "");
        outputfmt("// connections in={}\n", inst()->nconnections());
        outputfmt("// out={}\n", inst()->outgoing_connections());
        //         outputfmtn((inst()->writes_globals() ? " writes_globals" : ""););
        //         outputfmtn((inst()->userdata_params() ? " userdata_params" : ""););
        //         outputfmtn((inst()->run_lazily() ? " run_lazily" : " run_unconditionally"););
        //         outputfmtn((inst()->outgoing_connections() ? " outgoing_connections" : ""););
        //         outputfmtn((inst()->renderer_outputs() ? " renderer_outputs" : ""););
        //         outputfmtn((inst()->writes_globals() ? " writes_globals" : ""););
        //         outputfmtn((inst()->entry_layer() ? " entry_layer" : ""););
        //         outputfmtn((inst()->last_layer() ? " last_layer" : ""););
        //         outputfmtn("\n";);
        // outputfmt("//  symbols:\n");
        // for (size_t i = 0, e = inst()->symbols().size(); i < e; ++i) {
        //     outputfmt("// ");
        //     inst()->symbol(i)->print(m_out, 256);

        // }

        outputfmt("//  code:\n");
        outputfmt("{}void /*shader*/ {} (\n", indentstr(), inst()->layername());
        increment_indent();
        FOREACH_PARAM(Symbol & s, inst())
        {
            if (!s.everused())
                continue;  // skip unused symbols
            if (s.symtype() == SymTypeParam
                || s.symtype() == SymTypeOutputParam) {
                outputfmt("{}{}; /* = init TBD */\n", indentstr(),
                          cpp_var_declaration(s));
            }
        }
        decrement_indent();
        outputfmt("{}  )\n", indentstr());

        outputfmt("{}{{\n", indentstr());
        increment_indent();
        FOREACH_SYM(Symbol & s, inst())
        {
            using namespace OIIO::Strutil;
            if (!s.everused())
                continue;  // skip unused symbols
            if (s.symtype() == SymTypeConst) {
                outputfmt("{}{} = ", indentstr(), cpp_var_declaration(s));
                s.print_vals(m_out, 16);
                outputfmt(";\n");
            } else if (s.symtype() == SymTypeTemp
                       || s.symtype() == SymTypeLocal) {
                outputfmt("{}{};\n", indentstr(), cpp_var_declaration(s));
            }
        }
        build_cpp_code(0, int(inst()->ops().size()), false);

        decrement_indent();
        outputfmt("{}}}\n", indentstr());
        outputfmt("\n\n\n\n");
    }
}



void
BackendCpp::build_cpp_code(int opbegin, int opend, bool do_indent_block)
{
    if (do_indent_block) {
        outputfmt("{}{{\n", indentstr());
        increment_indent();
    }
    for (int opnum = opbegin; opnum < opend; ++opnum) {
        const Opcode& op(inst()->ops()[opnum]);
        if (opnum == (size_t)inst()->maincodebegin())
            outputfmt("{}// (main)\n", indentstr());
        auto* opdesc = shadingsys().op_descriptor(op.opname());
        if (opdesc && opdesc->cppgen) {
            // If the opcode has a C++ generator, call it
            if (!opdesc->cppgen(*this, opnum))
                outputfmt("{}// Cpp {} FAILED\n", indentstr(), op.opname());
        } else {
            // Otherwise, generate the default C++ code for it
            outputfmt("{}// NO CPP GENERATOR FOR {}\n", indentstr(),
                      op.opname());
        }

        // If the op we coded jumps around, skip past its recursive block
        // executions.
        int next = op.farthest_jump();
        if (next >= 0)
            opnum = next - 1;
    }
    if (do_indent_block) {
        decrement_indent();
        outputfmt("{}}}\n", indentstr());
    }
}



void
BackendCpp::op_gen_init()
{
    static std::mutex mutex;  // only one BackendCpp can do this at a time
    std::lock_guard<std::mutex> lock(mutex);

    // clang-format off
#define OP(name,cg)                                                     \
    if (auto* op = shadingsys().op_descriptormod(ustring(#name))) {     \
        if (op->cppgen) return; /* already set */                       \
        extern bool cpp_gen_##cg (BackendCpp &rop, int opnum);          \
        op->cppgen = cpp_gen_##cg;                                      \
    }

    // print("running BackendCpp::op_gen_init()\n");

    // name          cg gen               folder         simple     flags
    // OP (aassign,     aassign);
    OP (abs,         generic);
    OP (acos,        generic);
    OP (add,         binary_op);
    OP (and,         binary_op);
    // OP (area,        area);
    // OP (aref,        aref);
    // OP (arraycopy,   arraycopy);
    // OP (arraylength, arraylength);
    OP (asin,        generic);
    OP (assign,      assign);
    OP (atan,        generic);
    OP (atan2,       generic);
    // OP (backfacing,  get_simple_SG_field);
    OP (bitand,      binary_op);
    OP (bitor,       binary_op);
    // OP (blackbody,   blackbody);
    // OP (break,       loopmod_op);
    // OP (calculatenormal, calculatenormal);
    OP (cbrt,        generic);
    OP (ceil,        generic);
    OP (cellnoise,   generic /*noise*/);
    OP (clamp,       generic);
    // OP (closure,     closure);
    // OP (color,       construct_color);
    // OP (compassign,  compassign);
    OP (compl,       unary_op);
    // OP (compref,     compref);
    OP (concat,      generic);
    // OP (continue,    loopmod_op);
    OP (cos,         generic);
    OP (cosh,        generic);
    OP (cross,       generic);
    OP (degrees,     generic);
    OP (determinant, generic);
    // OP (dict_find,   dict_find);
    // OP (dict_next,   dict_next);
    // OP (dict_value,  dict_value);
    OP (distance,    generic);
    OP (div,         generic);
    OP (dot,         generic);
    // OP (Dx,          DxDy);
    // OP (Dy,          DxDy);
    // OP (Dz,          Dz);
    // OP (dowhile,     loop_op);
    OP (end,         nop);
    OP (endswith,    generic);
    // OP (environment, environment);
    OP (eq,          binary_op);
    OP (erf,         generic);
    OP (erfc,        generic);
    // OP (error,       printf);
    // OP (exit,        return);
    OP (exp,         generic);
    OP (exp2,        generic);
    OP (expm1,       generic);
    OP (fabs,        generic);
    // OP (filterwidth, filterwidth);
    OP (floor,       generic);
    OP (fmod,        generic);
    // OP (for,         loop_op);
    // OP (format,      printf);
    // OP (fprintf,     printf);
    // OP (functioncall, functioncall);
    // OP (functioncall_nr,functioncall_nr);
    OP (ge,          binary_op);
    // OP (getattribute, getattribute);
    OP (getchar,      generic);
    // OP (getmatrix,   getmatrix);
    // OP (getmessage,  getmessage);
    // OP (gettextureinfo, gettextureinfo);
    OP (gt,          binary_op);
    OP (hash,        generic);
    OP (hashnoise,   generic /*noise*/);
    OP (if,          if);
    OP (inversesqrt, generic);
    OP (isconnected, generic);
    // OP (isconstant,  isconstant);
    OP (isfinite,    generic);
    OP (isinf,       generic);
    OP (isnan,       generic);
    OP (le,          binary_op);
    OP (length,      generic);
    OP (log,         generic);
    OP (log10,       generic);
    OP (log2,        generic);
    OP (logb,        generic);
    OP (lt,          binary_op);
    OP (luminance,   generic);
    // OP (matrix,      matrix);
    OP (max,         generic);
    // OP (mxcompassign, mxcompassign);
    // OP (mxcompref,   mxcompref);
    OP (min,         generic);
    OP (mix,         generic);
    OP (mod,         generic);
    OP (mul,         binary_op);
    OP (neg,         unary_op);
    OP (neq,         binary_op);
    OP (noise,       generic /*noise*/);
    OP (nop,         nop);
    // OP (normal,      construct_triple);
    OP (normalize,   generic);
    OP (or,          binary_op);
    OP (pnoise,      generic /*noise*/);
    // OP (point,       construct_triple);
    // OP (pointcloud_search, pointcloud_search);
    // OP (pointcloud_get, pointcloud_get);
    // OP (pointcloud_write, );
    OP (pow,         generic);
    // OP (printf,      printf);
    OP (psnoise,     generic /*noise*/);
    OP (radians,     generic);
    // OP (raytype,     raytype);
    // OP (regex_match, regex);
    // OP (regex_search, regex);
    // OP (return,      return);
    OP (round,       generic);
    // OP (select,      select);
    // OP (setmessage,  setmessage);
    OP (shl,         binary_op);
    OP (shr,         binary_op);
    OP (sign,        generic);
    OP (sin,         generic);
    // OP (sincos,      sincos);
    OP (sinh,        generic);
    OP (smoothstep,  generic);
    OP (snoise,      generic /*noise*/);
    // OP (spline,      spline);
    // OP (splineinverse, spline);
    // OP (split,       split);
    OP (sqrt,        generic);
    OP (startswith,  generic);
    OP (step,        generic);
    OP (stof,        generic);
    OP (stoi,        generic);
    OP (strlen,      generic);
    OP (strtof,      generic);
    OP (strtoi,      generic);
    OP (sub,         binary_op);
    OP (substr,      generic);
    // OP (surfacearea, get_simple_SG_field);
    OP (tan,         generic);
    OP (tanh,        generic);
    OP (texture,     generic /* FIXME! texture*/);
    // OP (texture3d,   texture3d);
    // OP (trace,       trace);
    // OP (transform,   transform);
    // OP (transformc,  transformc);
    // OP (transformn,  transform);
    // OP (transformv,  transform);
    OP (transpose,   generic);
    OP (trunc,       generic);
    OP (useparam,    nop /*useparam*/);
    // OP (vector,      construct_triple);
    // OP (warning,     printf);
    // OP (wavelength_color, blackbody);
    // OP (while,       loop_op);
    OP (xor,         binary_op);
#undef OP
#undef OP2
    // clang-format on
}



// C++ code generator for no-ops: things that should be silent like giraffes
// when generating C++ code.
bool
cpp_gen_nop(BackendCpp& rop, int opnum)
{
    return true;
}



// C++ code generator for "generic" functions: just express it as a function
// call like:    result = osl_func(arg1, ...);
bool
cpp_gen_generic(BackendCpp& rop, int opnum)
{
    Opcode& op(rop.inst()->ops()[opnum]);
    OSL_DASSERT(op.nargs() >= 1);
    Symbol& R(*rop.inst()->argsymbol(op.firstarg() + 0));
    rop.outputfmt("{}{} = osl_{}(", rop.indentstr(), R.name(), op.opname());
    for (int a = 1; a < op.nargs(); ++a) {
        const Symbol* s(rop.inst()->argsymbol(op.firstarg() + a));
        if (a > 1)
            rop.outputfmt(", ");
        rop.outputfmt("{}", s->cpp_safe_name());
    }
    rop.outputfmt(");\n");
    return true;
}



// C++ code generator for "generic" functions: just express it as a function
// call like:    result = osl_func(arg1, ...);
bool
cpp_gen_if(BackendCpp& rop, int opnum)
{
    Opcode& op(rop.inst()->ops()[opnum]);
    Symbol& cond = *rop.opargsym(op, 0);

    // Then block
    rop.outputfmt("{}if ({})\n", rop.indentstr(), cond.cpp_safe_name());
    rop.build_cpp_code(opnum + 1, op.jump(0));
    if (op.jump(0) != op.jump(1)) {
        rop.outputfmt("{}else\n", rop.indentstr());
        rop.build_cpp_code(op.jump(0), op.jump(1));
    }
    return true;
}



bool
cpp_gen_assign(BackendCpp& rop, int opnum)
{
    Opcode& op(rop.inst()->ops()[opnum]);
    OSL_DASSERT(op.nargs() == 2);
    Symbol& R(*rop.inst()->argsymbol(op.firstarg() + 0));
    Symbol& A(*rop.inst()->argsymbol(op.firstarg() + 1));
    rop.outputfmt("{}{} = {};\n", rop.indentstr(), R.cpp_safe_name(),
                  A.cpp_safe_name());
    return true;
}



// unary ops
bool
cpp_gen_unary_op(BackendCpp& rop, int opnum)
{
    Opcode& op(rop.inst()->ops()[opnum]);
    OSL_DASSERT(op.nargs() == 2);
    Symbol& R(*rop.inst()->argsymbol(op.firstarg() + 0));
    Symbol& A(*rop.inst()->argsymbol(op.firstarg() + 1));
    const char* opsym = "UNKNOWN";
    if (op.opname() == "neg")
        opsym = "-";
    else if (op.opname() == "compl")
        opsym = "~";

    else
        OSL_ASSERT_MSG(0, "Unknown unary op %s", op.opname().c_str());
    rop.outputfmt("{}{} = {} {};\n", rop.indentstr(), R.cpp_safe_name(),
                  opsym, A.cpp_safe_name());
    return true;
}



// binary ops
bool
cpp_gen_binary_op(BackendCpp& rop, int opnum)
{
    Opcode& op(rop.inst()->ops()[opnum]);
    OSL_DASSERT(op.nargs() == 3);
    Symbol& R(*rop.inst()->argsymbol(op.firstarg() + 0));
    Symbol& A(*rop.inst()->argsymbol(op.firstarg() + 1));
    Symbol& B(*rop.inst()->argsymbol(op.firstarg() + 2));
    const char* opsym = "UNKNOWN";
    if (op.opname() == "add")
        opsym = "+";
    else if (op.opname() == "sub")
        opsym = "-";
    else if (op.opname() == "mul")
        opsym = "*";

    else if (op.opname() == "eq")
        opsym = "==";
    else if (op.opname() == "neq")
        opsym = "!=";
    else if (op.opname() == "lt")
        opsym = "<";
    else if (op.opname() == "gt")
        opsym = ">";
    else if (op.opname() == "le")
        opsym = "<=";
    else if (op.opname() == "ge")
        opsym = ">=";

    else if (op.opname() == "bitand")
        opsym = "&";
    else if (op.opname() == "bitor")
        opsym = "|";
    else if (op.opname() == "xor")
        opsym = "^";
    else if (op.opname() == "shl")
        opsym = "<<";
    else if (op.opname() == "shr")
        opsym = ">>";

    else if (op.opname() == "and")
        opsym = "&&";
    else if (op.opname() == "or")
        opsym = "||";

    else
        OSL_ASSERT_MSG(0, "Unknown binary op %s", op.opname().c_str());
    rop.outputfmt("{}{} = {} {} {};\n", rop.indentstr(), R.cpp_safe_name(),
                  A.cpp_safe_name(), opsym, B.cpp_safe_name());
    return true;
}



};  // namespace pvt
OSL_NAMESPACE_END
