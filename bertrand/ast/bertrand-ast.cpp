#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
// #include "llvm/Support/Casting.h"

#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>


using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;


/* Command-line options for the tool. */
static cl::OptionCategory TOOL_CATEGORY("my-tool options");
static cl::extrahelp COMMON_HELP(CommonOptionsParser::HelpMessage);
static cl::extrahelp THIS_HELP("\nMore help text...");


// https://www.kdab.com/cpp-with-clang-libtooling/
// https://xinhuang.github.io/posts/2015-02-08-clang-tutorial-the-ast-matcher.html

// https://clang.llvm.org/doxygen/classclang_1_1ImportDecl.html
// https://clang.llvm.org/doxygen/classclang_1_1ExportDecl.html
// https://clang.llvm.org/doxygen/classclang_1_1Module.html



static std::unordered_map<std::string, std::string> output {
    {"has_main", "False"},
    {"is_primary_module_interface", "False"},
    {"exports", "{"}
};


struct MainFunction : public MatchFinder::MatchCallback {
    inline static auto pattern = functionDecl(
        isMain()
    ).bind("main");

    void run(const MatchFinder::MatchResult& result) override {
        const auto* main_decl =
            result.Nodes.getNodeAs<FunctionDecl>("main");
        output["has_main"] = "True";
    }

};


// struct Import : public MatchFinder::MatchCallback {
//     inline static auto pattern = decl().bind("import");

//     void run(const MatchFinder::MatchResult& result) override {
//         if (const auto* import_decl = dyn_cast<ImportDecl>(
//             result.Nodes.getNodeAs<Decl>("import")
//         )) {
//             for (const auto* decl : import_decl->decls()) {
//                 output["imports"] = "True";
//             }
//         }
//     }

// };


// TODO: AST Parsing is sensitive to unresolved imports, so Python bindings need to
// be generated immediately after clang-scan-deps, and the AST parser can only be
// called afterwards.  That means I can't use the AST parser to determine whether a
// module should be built as a shared library or an executable.

// -> Maybe there's a way to configure a simplified CMakeLists.txt file to emit
// the dependency graph without needing to know which modules are shared libraries
// and which are executables.
// -> Indeed there is.  Since all we need is the dependency graph, we can just
// add all the sources to a hypothetical shared library, emit compile-commands.json,
// and then run clang-scan-deps using it to generate the dependency graph.  I can then
// identify primary module interfaces by analyzing the logical-name field and generate
// Python modules for any unresolved imports.  This should allow me to construct the
// final CMakeLists.txt file, which I then pass to the AST parser to generate Python
// bindings.


struct Export : public MatchFinder::MatchCallback {
    inline static auto pattern = decl().bind("export");

    void run(const MatchFinder::MatchResult& result) override {
        if (const auto* export_decl = dyn_cast<ExportDecl>(
            result.Nodes.getNodeAs<Decl>("export")
        )) {
            for (const auto* decl : export_decl->decls()) {
                // global variable
                if (const auto* var_decl =
                    dyn_cast<VarDecl>(decl)
                ) {
                    std::string& map = output["exports"];
                    map += "\n        \"";
                    map += var_decl->getNameAsString();
                    map += "\": {";
                    // TODO: insert data related to: type, value etc.
                    map += "\n        },";

                // global function
                } else if (const auto* func_decl =
                    dyn_cast<FunctionDecl>(decl)
                ) {
                    std::string& map = output["exports"];
                    map += "\n        \"";
                    map += func_decl->getNameAsString();
                    map += "\": {";
                    // TODO: insert data related to: return type, parameters etc.
                    map += "\n        },";

                // class
                } else if (const auto* class_decl =
                    dyn_cast<CXXRecordDecl>(decl)
                ) {
                    std::string& map = output["exports"];
                    map += "\n        \"";
                    map += class_decl->getNameAsString();
                    map += "\": {";
                    // TODO: insert data related to: methods, fields etc.
                    map += "\n        },";
                }
            }
        }
        // } else if (
        //     const auto* decl = result.Nodes.getNodeAs<Decl>("export")
        // ) {
        //     const SourceManager& src = *result.SourceManager;
        //     SourceLocation start = decl->getBeginLoc();
        //     SourceLocation end = Lexer::getLocForEndOfToken(
        //         decl->getEndLoc(),
        //         0,
        //         src,
        //         result.Context->getLangOpts()
        //     );
        //     std::string decltext = Lexer::getSourceText(
        //         CharSourceRange::getCharRange(start, end),
        //         src,
        //         result.Context->getLangOpts()
        //     ).str();
        //     outs() << decltext << "\n";
        //     if (decltext.find("export module") != std::string::npos) {
        //         output["is_primary_module_interface"] = "True";
        //     }
        // }
    }

};


int main(int argc, const char** argv) {
    auto parser = CommonOptionsParser::create(argc, argv, TOOL_CATEGORY);
    if (!parser) {
        errs() << parser.takeError();
        return 1;
    }
    CommonOptionsParser& options = parser.get();
    ClangTool tool(
        options.getCompilations(),
        options.getSourcePathList()
    );

    MatchFinder finder;
    MainFunction main_function;
    Export exports;

    finder.addMatcher(Export::pattern, &exports);
    finder.addMatcher(MainFunction::pattern, &main_function);
    auto result = tool.run(newFrontendActionFactory(&finder).get());
    if (result == 0) {
        output["exports"] += "\n    }";

        outs() << "{\n";
        auto it = output.begin();
        auto end = output.end();
        if (it != end) {
            outs() << "    \"" << it->first << "\": " << it->second;
            ++it;
        }
        while (it != end) {
            outs() << ",\n    \"" << it->first << "\": " << it->second;
            ++it;
        }
        outs() << "\n}\n";
    }
    return result;
}
