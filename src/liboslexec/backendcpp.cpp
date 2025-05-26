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



void
BackendCpp::run()
{
    auto& out [[maybe_unused]]   = std::cout;
    int nlayers = (int)group().nlayers();
    for (int layer = 0; layer < nlayers; ++layer) {
        set_inst(layer);
        if (inst()->unused())
            continue;  // no need to print or gather stats for unused layers
        find_basic_blocks();

        outputfmt("// Layer {}: {}\n", layer, inst()->layername());

        outputfmtln("// Shader {}{}", inst()->shadername(),
                    inst()->unused() ? " (UNUSED)" : "");
        outputfmtln("// connections in={}", inst()->nconnections());
        outputfmtln("// out={}", inst()->outgoing_connections());
        //         outputfmtln((inst()->writes_globals() ? " writes_globals" : ""););
        //         outputfmtln((inst()->userdata_params() ? " userdata_params" : ""););
        //         outputfmtln((inst()->run_lazily() ? " run_lazily" : " run_unconditionally"););
        //         outputfmtln((inst()->outgoing_connections() ? " outgoing_connections" : ""););
        //         outputfmtln((inst()->renderer_outputs() ? " renderer_outputs" : ""););
        //         outputfmtln((inst()->writes_globals() ? " writes_globals" : ""););
        //         outputfmtln((inst()->entry_layer() ? " entry_layer" : ""););
        //         outputfmtln((inst()->last_layer() ? " last_layer" : ""););
        //         outputfmtln("\n";);
        outputfmtln("//  symbols:");
        for (size_t i = 0, e = inst()->symbols().size(); i < e; ++i) {
            outputfmt("// ");
            inst()->symbol(i)->print(std::cout, 256);
        }
        outputfmtln("//  code:");
        outputfmtln("{{\n");
        increment_indent();
        for (size_t i = 0, e = inst()->ops().size(); i < e; ++i) {
            const Opcode& op(inst()->ops()[i]);
            if (i == (size_t)inst()->maincodebegin())
                outputfmt("{}// (main)\n", indentstr());
            // outputfmt("{}// {}: {}", indentstr(), i, op.opname());
            // bool allconst = true;
            // for (int a = 0; a < op.nargs(); ++a) {
            //     const Symbol* s(inst()->argsymbol(op.firstarg() + a));
            //     outputfmt(" {}", s->name());
            //     if (s->symtype() == SymTypeConst) {
            //         outputfmt(" (");
            //         s->print_vals(out, 16);
            //         outputfmt(")");
            //     }
            //     if (op.argread(a))
            //         allconst &= s->is_constant();
            // }
            // for (size_t j = 0; j < Opcode::max_jumps; ++j)
            //     if (op.jump(j) >= 0)
            //         outputfmt(" {}", op.jump(j));
            // outputfmt("\t# ");
            // //        out << "    rw " << fmtformat("{:x}", op.argread_bits())
            // //            << ' ' << op.argwrite_bits();
            // if (op.argtakesderivs_all())
            //     outputfmt(" %derivs({}) ", op.argtakesderivs_all());
            // if (allconst)
            //     outputfmt("  CONST");
            // if (i == 0 || bblockid(i) != bblockid(i - 1))
            //     outputfmt("  BBLOCK-START");
            // std::string filename = op.sourcefile().string();
            // size_t slash         = filename.find_last_of("/");
            // if (slash != std::string::npos)
            //     filename.erase(0, slash + 1);
            // if (filename.length())
            //     outputfmt("  ({}:{})", filename, op.sourceline());
            // outputfmt("\n");

            auto* opdesc = shadingsys().op_descriptor(op.opname());
            if (opdesc && opdesc->cppgen) {
                // If the opcode has a C++ generator, call it
                if (!opdesc->cppgen(*this, i))
                    outputfmt("{}// Cpp {} FAILED\n", indentstr(), op.opname());
            } else {
                // Otherwise, generate the default C++ code for it
                outputfmt("{}// NO CPP GENERATOR FOR {}\n", indentstr(),
                          op.opname());
            }
            // outputfmt("{}{}(", indentstr(), op.opname());
            // for (int a = 0; a < op.nargs(); ++a) {
            //     const Symbol* s(inst()->argsymbol(op.firstarg() + a));
            //     outputfmt("{}{}", a == 0 ? "" : ", ", s->name());
            // }
            // outputfmt(";\n");
        }
        decrement_indent();
        outputfmtln("}}\n\n\n");
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
    OP (add,         binop);
    // OP (and,         andor);
    // OP (area,        area);
    // OP (aref,        aref);
    // OP (arraycopy,   arraycopy);
    // OP (arraylength, arraylength);
    OP (asin,        generic);
    // OP (assign,      assign);
    OP (atan,        generic);
    OP (atan2,       generic);
    // OP (backfacing,  get_simple_SG_field);
    OP (bitand,      binop);
    OP (bitor,       binop);
    // OP (blackbody,   blackbody);
    // OP (break,       loopmod_op);
    // OP (calculatenormal, calculatenormal);
    OP (cbrt,        generic);
    OP (ceil,        generic);
    // OP (cellnoise,   noise);
    // OP (clamp,       clamp);
    // OP (closure,     closure);
    // OP (color,       construct_color);
    // OP (compassign,  compassign);
    // OP (compl,       unary_op);
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
    // OP (div,         div);
    OP (dot,         generic);
    // OP (Dx,          DxDy);
    // OP (Dy,          DxDy);
    // OP (Dz,          Dz);
    // OP (dowhile,     loop_op);
    OP (end,         nop);
    OP (endswith,    generic);
    // OP (environment, environment);
    // OP (eq,          compare_op);
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
    // OP (fmod,        modulus);
    // OP (for,         loop_op);
    // OP (format,      printf);
    // OP (fprintf,     printf);
    // OP (functioncall, functioncall);
    // OP (functioncall_nr,functioncall_nr);
    // OP (ge,          compare_op);
    // OP (getattribute, getattribute);
    OP (getchar,      generic);
    // OP (getmatrix,   getmatrix);
    // OP (getmessage,  getmessage);
    // OP (gettextureinfo, gettextureinfo);
    // OP (gt,          compare_op);
    OP (hash,        generic);
    // OP (hashnoise,   noise);
    // OP (if,          if);
    OP (inversesqrt, generic);
    OP (isconnected, generic);
    // OP (isconstant,  isconstant);
    OP (isfinite,    generic);
    OP (isinf,       generic);
    OP (isnan,       generic);
    // OP (le,          compare_op);
    OP (length,      generic);
    OP (log,         generic);
    OP (log10,       generic);
    OP (log2,        generic);
    OP (logb,        generic);
    // OP (lt,          compare_op);
    // OP (luminance,   luminance);
    // OP (matrix,      matrix);
    OP (max,         generic);
    // OP (mxcompassign, mxcompassign);
    // OP (mxcompref,   mxcompref);
    OP (min,         generic);
    OP (mix,         generic);
    // OP (mod,         modulus);
    OP (mul,         binop);
    // OP (neg,         neg);
    // OP (neq,         compare_op);
    // OP (noise,       noise);
    OP (nop,         nop);
    // OP (normal,      construct_triple);
    OP (normalize,   generic);
    // OP (or,          andor);
    // OP (pnoise,      noise);
    // OP (point,       construct_triple);
    // OP (pointcloud_search, pointcloud_search);
    // OP (pointcloud_get, pointcloud_get);
    // OP (pointcloud_write, );
    OP (pow,         generic);
    // OP (printf,      printf);
    // OP (psnoise,     noise);
    OP (radians,     generic);
    // OP (raytype,     raytype);
    // OP (regex_match, regex);
    // OP (regex_search, regex);
    // OP (return,      return);
    OP (round,       generic);
    // OP (select,      select);
    // OP (setmessage,  setmessage);
    OP (shl,         binop);
    OP (shr,         binop);
    OP (sign,        generic);
    OP (sin,         generic);
    // OP (sincos,      sincos);
    OP (sinh,        generic);
    OP (smoothstep,  generic);
    // OP (snoise,      noise);
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
    OP (sub,         binop);
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
    OP (xor,         binop);
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
        rop.outputfmt("{}", s->name());
    }
    rop.outputfmt(");\n");
    return true;
}



// binary ops
bool
cpp_gen_binop(BackendCpp& rop, int opnum)
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
    else
        OSL_ASSERT_MSG(0, "Unknown binary op %s", op.opname().c_str());
    rop.outputfmt("{}{} = {} {} {};\n", rop.indentstr(), R.name(), A.name(),
                  opsym, B.name());
    return true;
}



};  // namespace pvt
OSL_NAMESPACE_END
