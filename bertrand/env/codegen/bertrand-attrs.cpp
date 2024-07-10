
#include "clang/AST/Decl.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/ParsedAttrInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"


// TODO: attributes defined in a plugin will never be recognized by clangd.  In order
// to fix that, we need to add the attribute to the clang registry using tablegen.

// https://www.cs.cmu.edu/~seth/llvm/llvmannotation.html


namespace {


/* ParsedAttrInfo types represent C++-style [[attributes]] that can be attached to
 * declarations in the AST.  These can be used to control the automated binding
 * generator in various ways.
 *
 * https://clang.llvm.org/doxygen/structclang_1_1ParsedAttrInfo.html
 * https://clang.llvm.org/docs/InternalsManual.html#how-to-add-an-attribute
 */


// https://www.cs.cmu.edu/~seth/llvm/llvmannotation.html


class NoExport : public clang::ParsedAttrInfo {

public:

    NoExport() {
        static constexpr Spelling S[] = {
            {clang::ParsedAttr::AS_C23, "py::noexport"},
            {clang::ParsedAttr::AS_CXX11, "py::noexport"},
        };
        Spellings = S;
    }

    bool diagAppertainsToDecl(
        clang::Sema& sema,
        const clang::ParsedAttr& attr,
        const clang::Decl* decl
    ) const override {
        // if (!clang::isa<clang::FunctionDecl>(decl)) {
        //     // sema.Diag(attr.getLoc(), clang::diag::err_attribute_not_type_attr)
        //     //     << attr << attr.isRegularKeywordAttribute() << "export";
        //     return false;
        // }
        return true;
    }

    AttrHandling handleDeclAttribute(
        clang::Sema& sema,
        clang::Decl* decl,
        const clang::ParsedAttr& attr
    ) const override {
        decl->addAttr(clang::AnnotateAttr::Create(
            sema.Context,
            "noexport",
            nullptr,
            0,
            attr.getRange()
        ));
        return AttributeApplied;
    }

};


}


/* Register the attributes with the clang driver. */
static clang::ParsedAttrInfoRegistry::Add<NoExport> noexport_attr(
    "py::noexport",
    "disable automatic Python bindings for this export declaration"
);
