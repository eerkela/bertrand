#include <fstream>
#include <ios>
#include <string>

#include "clang/AST/Decl.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"

// file locking is not cross-platform
#ifdef _WIN32
    #include <Windows.h>
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/file.h>
#endif





// TODO: ok, this looks like it works, and should be a lot more efficient/easier to
// work with than libtooling.  Now, this file's job is to analyze the AST and emit a
// Python binding file if any export declarations are present.  It should also write
// out a simple .json file alongside the build targets that lists whether or not this
// file is an executable, and what it exports if so.

// -> Perhaps it's better to only emit the JSON, which would include binding
// information.  That would allow me to avoid generating python bindings for
// non-primary module interfaces.
// -> Alternatively, I could pass a flag to the plugin that indicates whether each
// object is a primary module interface.  If this is toggled off, then the plugin
// won't emit any bindings for that file, and all the callbacks will just pass.  Then,
// the primary callback would emit the bindings in one big chunk.  That's probably
// the way to go.

// That means that this file is almost entirely devoted to binding generation, since
// the metadata json will barely contain any information.  Note also that if the
// path to the python bindings already exists, then the plugin will need to generate
// the bindings and then compare them to the existing file, in order not to overwrite
// them and allow incremental builds.


// https://www.youtube.com/watch?v=A9COzFs-gEg


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
        // all templates is similar to not using -fdelayed-template-parsing in the
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



namespace impl {

    class FileLock {
        std::string path;
        #ifdef _WIN32
            HANDLE handle;
        #else
            int handle;
        #endif

    public:

        #ifdef _WIN32

            FileLock(const std::string& file) : path(file), handle(CreateFileA(
                path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                NULL,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            )) {
                if (handle == INVALID_HANDLE_VALUE) {
                    llvm::errs() << "Failed to open file: " << path << "\n";
                } else {
                    OVERLAPPED overlapped = {0};
                    if (!LockFileEx(
                        handle,
                        LOCKFILE_EXCLUSIVE_LOCK,
                        0,
                        MAXDWORD,
                        MAXDWORD,
                        &overlapped
                    )) {
                        CloseHandle(handle);
                        handle = INVALID_HANDLE_VALUE;
                        llvm::errs() << "Failed to lock file: " << path << "\n";
                    }
                }
            }

            ~FileLock() {
                if (handle != INVALID_HANDLE_VALUE) {
                    OVERLAPPED overlapped = {0};
                    UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &overlapped);
                    CloseHandle(handle);
                }
            }

        #else

            FileLock(const std::string& file) : path(file), handle(
                open(path.c_str(), O_RDWR | O_CREAT, 0666)
            ) {
                if (handle == -1) {
                    llvm::errs() << "Failed to open file: " << path << "\n";
                } else if (flock(handle, LOCK_EX) == -1) {
                    close(handle);
                    llvm::errs() << "Failed to lock file: " << path << "\n";
                }
            }

            ~FileLock() {
                if (handle != -1) {
                    if (flock(handle, LOCK_UN) == -1) {
                        llvm::errs() << "Failed to unlock file: " << path << "\n";
                    }
                    close(handle);
                }
            }

        #endif

    };

}


////////////////////////
////    VISITORS    ////
////////////////////////


/* RecursiveASTVisitors visit each node in the AST and are triggered by overriding
 * Visit{Foo}(Foo*) methods, where Foo is a type of node in the clang AST, given here:
 *
 * https://clang.llvm.org/doxygen/classclang_1_1Decl.html
 * https://clang.llvm.org/doxygen/classclang_1_1Stmt.html
 */


class ExportVisitor : public clang::RecursiveASTVisitor<ExportVisitor> {
    std::string path;  // TODO: this can probably be kept in the ASTConsumer
    std::string body;

    // TODO: need a lot of support functions to extract out the necessary information

    // TODO: maybe I should use a separate class to handle the file I/O, so that I
    // can incrementally build the binding file.  I believe that's necessary anyways
    // since the AST parser will be invoked separately for each translation unit, so
    // I need some way to incrementally write the bindings.  I might be able to just
    // delete the closing brace and then continue appending.

public:

    // TODO: every time we visit an export declaration, update the body of the Python
    // binding file accordingly.  There will also need to be a method to write the
    // header and footer of the file, and to write the file to disk.

    ExportVisitor(const std::string& path) : path(path) {}

    bool VisitExportDecl(clang::ExportDecl* export_decl) {
        using llvm::dyn_cast;

        int count = 0;

        for (const auto* decl : export_decl->decls()) {
            ++count;
            if (auto* func = dyn_cast<clang::FunctionDecl>(decl)) {
                llvm::errs() << "exported function " << func->getNameAsString() << "\n";

            // CXXRecordDecl?
            } else if (auto* type = dyn_cast<clang::TypeDecl>(decl)) {
                llvm::errs() << "exported type " << type->getNameAsString() << "\n";

            } else if (auto* var = dyn_cast<clang::VarDecl>(decl)) {
                llvm::errs() << "exported variable " << var->getNameAsString() << "\n";

            } else if (auto* name = dyn_cast<clang::NamespaceDecl>(decl)) {
                llvm::errs() << "exported namespace " << name->getNameAsString() << "\n";

            } else {
                llvm::errs() << "unhandled export " << decl->getDeclKindName() << "\n";
            }
        }

        llvm::errs() << "exported " << count << " declarations\n";

        return true;
    }

};


/////////////////////////
////    CONSUMERS    ////
/////////////////////////


/* ASTConsumers are responsible for triggering the AST visitors at some point during
 * the build process.
 *
 * Overriding HandleTranslationUnit will parse the complete AST for an entire
 * translation unit after it has been constructed, and will typically use a 
 * RecursiveASTVisitor to do so.  This triggers an extra pass over the AST, so it's a
 * bit more expensive as a result.  It does, however, grant access to the complete
 * AST.
 *
 * It is also possible to trigger the visitor on every top-level declaration as it is
 * parsed by overriding HandleTopLevelDecl.  This is more efficient and can avoid an
 * extra pass over the AST, but can only be used when the visitor doesn't need to see
 * the complete AST.
 *
 * https://clang.llvm.org/doxygen/classclang_1_1ASTConsumer.html
 */


class ExportConsumer : public clang::ASTConsumer {
    clang::CompilerInstance& compiler;
    bool python;
    std::string path;
    std::string cache_path;

public:

    ExportConsumer(
        clang::CompilerInstance& compiler,
        bool python,
        std::string path,
        std::string cache_path
    ) : compiler(compiler), python(python), path(path), cache_path(cache_path) {}

    void HandleTranslationUnit(clang::ASTContext& context) override {
        if (python) {
            // open (or create) and lock the cache file within this context (RAII)
            impl::FileLock lock(cache_path);

            // get the source path
            auto& src_mgr = context.getSourceManager();
            auto main_file = src_mgr.getFileEntryRefForID(
                src_mgr.getMainFileID()
            );
            std::string source_path = main_file ? main_file->getName().str() : "";
            if (source_path.empty()) {
                llvm::errs() << "failed to get path for source of: " << path << "\n";
                return;
            }

            // check for a cached entry for this path or write a new one
            bool cached = false;
            std::fstream file(cache_path, std::ios::in | std::ios::out);
            if (file) {
                std::string line;
                while (std::getline(file, line)) {
                    if (line == source_path) {
                        cached = true;
                        break;
                    }
                }
                if (!cached) {
                    ExportVisitor visitor(path);
                    visitor.TraverseDecl(context.getTranslationUnitDecl());
                    file.clear();  // clear EOF flag before writing
                    file.seekp(0, std::ios::end);  // append to end of file
                    file << source_path << '\n';
                }
                file.close();
            } else {
                llvm::errs() << "failed to open cache file: " << cache_path << "\n";
            }
        }
    }

};


class MainConsumer : public clang::ASTConsumer {
    clang::CompilerInstance& compiler;

public:

    MainConsumer(clang::CompilerInstance& compiler) : compiler(compiler) {}

    bool HandleTopLevelDecl(clang::DeclGroupRef decl_group) override {
        for (const clang::Decl* decl : decl_group) {
            auto* func = llvm::dyn_cast<clang::FunctionDecl>(decl);
            if (func && func->isMain()) {
                llvm::errs() << "found main function\n";
            }
        }
        return true;
    }

};


///////////////////////
////    ACTIONS    ////
///////////////////////


/* PluginASTActions handle config and ordering for the plugin, and are invoked by the
 * compiler driver.  The relevant fields are as follows:
 *
 *  getActionType: specifies when the plugin should be run in relation to other
 *      compilation tasks.  Typically, one should run the plugin just before the main
 *      compilation step, so that the plugin can modify the AST and avoid excess memory
 *      usage.
 *  CreateASTConsumer: instantiates an ASTConsumer to schedule the plugin's work on the
 *      AST.  Dynamic polymorphism is used for type erasure.
 *  ParseArgs: parses command-line arguments for the plugin, which have to be provided
 *      in a specific format described by the Clang Plugins documentation.
 *      https://clang.llvm.org/docs/ClangPlugins.html
 *
 * https://clang.llvm.org/doxygen/classclang_1_1PluginASTAction.html
 */


class ExportAction : public clang::PluginASTAction {
    bool python;
    std::string path;
    std::string cache_path;

protected:

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& compiler,
        llvm::StringRef
    ) override {
        return std::make_unique<ExportConsumer>(compiler, python, path, cache_path);
    }

    bool ParseArgs(
        const clang::CompilerInstance& compiler,
        const std::vector<std::string>& args
    ) override {
        bool cache_given = false;
        for (const std::string& arg : args) {
            if (arg.starts_with("python=")) {
                python = true;
                path = arg.substr(arg.find('=') + 1);

            } else if (arg.starts_with("cache=")) {
                cache_given = true;
                cache_path = arg.substr(arg.find('=') + 1);

            } else {
                clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
                unsigned diagnostics_id = diagnostics.getCustomDiagID(
                    clang::DiagnosticsEngine::Error,
                    "invalid argument '%0'"
                );
                diagnostics.Report(diagnostics_id) << arg;
                return false;
            }
        }

        if (!cache_given) {
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            unsigned diagnostics_id = diagnostics.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "missing required argument 'cache=...'"
            );
            diagnostics.Report(diagnostics_id);
            return false;
        }

        return true;
    }

    PluginASTAction::ActionType getActionType() override {
        return AddBeforeMainAction;
    }

};


class MainAction : public clang::PluginASTAction {
protected:

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& compiler,
        llvm::StringRef
    ) override {
        return std::make_unique<MainConsumer>(compiler);
    }

    bool ParseArgs(
        const clang::CompilerInstance& compiler,
        const std::vector<std::string>& args
    ) override {
        // TODO: allow a path to be passed in for the cache file that is used to
        // pass back up to the setup script.

        // for (const std::string& arg : args) {
        //     if (arg.starts_with("python=")) {
        //         python = true;
        //         path = arg.substr(arg.find('=') + 1);

        //     } else {
        //         clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
        //         unsigned diagnostics_id = diagnostics.getCustomDiagID(
        //             clang::DiagnosticsEngine::Error,
        //             "invalid argument '%0'"
        //         );
        //         diagnostics.Report(diagnostics_id) << arg;
        //         return false;
        //     }
        // }

        return true;
    }

    PluginASTAction::ActionType getActionType() override {
        return AddBeforeMainAction;
    }

};


}


/* Register the plugin with the clang driver. */
static clang::FrontendPluginRegistry::Add<ExportAction> export_action(
    "export",
    "emit a Python binding file for each primary module interface unit, and "
    "gather contextual information for Bertrand's automated build system."
);
static clang::FrontendPluginRegistry::Add<MainAction> main_action(
    "main",
    "detect a main() entry point in the AST and direct Bertrand's automated "
    "build system to compile a matching executable."
);
