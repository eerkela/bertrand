#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"


namespace {


class PrintFunctionsConsumer : public clang::ASTConsumer {
    clang::CompilerInstance& instance;
    std::set<std::string> parsed_templates;

public:

    PrintFunctionsConsumer(
        clang::CompilerInstance& instance,
        std::set<std::string> parsed_templates
    ) : instance(instance), parsed_templates(parsed_templates)
    {}

    bool HandleTopLevelDecl(clang::DeclGroupRef decl_group) override {
        for (const clang::Decl* decl : decl_group) {
            const clang::NamedDecl* named = clang::dyn_cast<clang::NamedDecl>(decl);
            if (named) {
                llvm::errs() << "top-level-decl: \"" << named->getNameAsString() << "\"\n";
            }
        }
        return true;
    }

    void HandleTranslationUnit(clang::ASTContext& context) override {
        if (!instance.getLangOpts().DelayedTemplateParsing)
            return;

        // This demonstrates how to force instantiation of some templates in
        // -fdelayed-template-parsing mode. (Note: Doing this unconditionally for
        // all templates is similar to not using -fdelayed-template-parsig in the
        // first place.)
        // The advantage of doing this in HandleTranslationUnit() is that all
        // codegen (when using -add-plugin) is completely finished and this can't
        // affect the compiler output.
        struct Visitor : public clang::RecursiveASTVisitor<Visitor> {
            const std::set<std::string> &parsed_templates;
            std::set<clang::FunctionDecl*> late_parsed;

            Visitor(const std::set<std::string>& parsed_templates) :
                parsed_templates(parsed_templates)
            {}

            bool VisitFunctionDecl(clang::FunctionDecl* func) {
                if (
                    func->isLateTemplateParsed() &&
                    parsed_templates.count(func->getNameAsString())
                ) {
                    late_parsed.insert(func);
                }
                return true;
            }
        };

        Visitor visitor(parsed_templates);
        visitor.TraverseDecl(context.getTranslationUnitDecl());

        clang::Sema& sema = instance.getSema();
        for (const clang::FunctionDecl* func : visitor.late_parsed) {
            clang::LateParsedTemplate& late =
                *sema.LateParsedTemplateMap.find(func)->second;
            sema.LateTemplateParser(sema.OpaqueParser, late);
            llvm::errs() << "late-parsed-decl: \"" << func->getNameAsString() << "\"\n";
        }
    }
};


class PrintFunctionNamesAction : public clang::PluginASTAction {
    std::set<std::string> parsed_templates;

protected:

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& CI,
        llvm::StringRef
    ) override {
        return std::make_unique<PrintFunctionsConsumer>(CI, parsed_templates);
    }

    bool ParseArgs(
        const clang::CompilerInstance& compiler,
        const std::vector<std::string>& args
    ) override {
        for (unsigned i = 0, e = args.size(); i != e; ++i) {
            llvm::errs() << "PrintFunctionNames arg = " << args[i] << "\n";

            // Example error handling.
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            if (args[i] == "-an-error") {
                unsigned DiagID = diagnostics.getCustomDiagID(
                    clang::DiagnosticsEngine::Error,
                    "invalid argument '%0'"
                );
                diagnostics.Report(DiagID) << args[i];
                return false;
            } else if (args[i] == "-parse-template") {
                if (i + 1 >= e) {
                    diagnostics.Report(diagnostics.getCustomDiagID(
                        clang::DiagnosticsEngine::Error,
                        "missing -parse-template argument"
                    ));
                    return false;
                }
                ++i;
                parsed_templates.insert(args[i]);
            }
        }

        if (!args.empty() && args[0] == "help") {
            PrintHelp(llvm::errs());
        }

        return true;
    }

    void PrintHelp(llvm::raw_ostream& ros) {
        ros << "Help for PrintFunctionNames plugin goes here\n";
    }

    PluginASTAction::ActionType getActionType() override {
        return AddBeforeMainAction;
    }

};


}


static clang::FrontendPluginRegistry::Add<PrintFunctionNamesAction> X(
    "print-fns", "print function names"
);
