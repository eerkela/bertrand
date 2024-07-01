#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>


/* All command-line parsers have a separate category to group options. */
static llvm::cl::OptionCategory TOOL_CATEGORY("my-tool options");


/* CommonOptionsParser declares HelpMessage with a description of the common
command-line options related to the compilation database and input files.  This
message should be displayed in all tools. */
static llvm::cl::extrahelp COMMON_HELP(clang::tooling::CommonOptionsParser::HelpMessage);


/* A help message for this specific tool. */
static llvm::cl::extrahelp MORE_HELP("\nMore help text...");


int main(int argc, const char** argv) {
    auto parser = clang::tooling::CommonOptionsParser::create(
        argc,
        argv,
        TOOL_CATEGORY
    );
    if (!parser) {
        llvm::errs() << parser.takeError();
        return 1;
    }
    clang::tooling::CommonOptionsParser& options = parser.get();
    clang::tooling::ClangTool tool(
        options.getCompilations(),
        options.getSourcePathList()
    );
    return tool.run(
        clang::tooling::newFrontendActionFactory<clang::SyntaxOnlyAction>().get()
    );
}
