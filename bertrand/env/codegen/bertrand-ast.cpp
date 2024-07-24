#include <fstream>
#include <ios>
#include <sstream>
#include <string>

#include "clang/AST/Decl.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/ParsedAttrInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"

// file locks are not cross-platform
#ifdef _WIN32
    #include <Windows.h>
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/file.h>
#endif

#include <nlohmann/json.hpp>
using json = nlohmann::json;


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

    /* An RAII-based file handle that acts like a std:fstream, but also holds an
    OS-level file lock to prevent concurrent writes.  Note that this creates the file
    if it did not previously exist, and reads and writes from it 1 line at a time,
    meaning consumers should not need to append manual newline characters.  The `->`
    operator allows access to named fstream methods. */
    struct FileLock {
        std::string path;
        std::fstream file;

        #ifdef _WIN32
            HANDLE handle;

            FileLock(const std::string& filepath) : path(filepath), handle(CreateFileA(
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
                    } else {
                        file.open(path, std::ios::in | std::ios::out);
                    }
                }
            }

            ~FileLock() {
                if (handle != INVALID_HANDLE_VALUE) {
                    file.close();
                    OVERLAPPED overlapped = {0};
                    UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &overlapped);
                    CloseHandle(handle);
                }
            }

        #else
            int handle;

            FileLock(const std::string& filepath) : path(filepath), handle(
                open(path.c_str(), O_RDWR | O_CREAT, 0666)
            ) {
                if (handle == -1) {
                    llvm::errs() << "Failed to open file: " << path << "\n";
                } else if (flock(handle, LOCK_EX) == -1) {
                    close(handle);
                    llvm::errs() << "Failed to lock file: " << path << "\n";
                } else {
                    file.open(path, std::ios::in | std::ios::out);
                }
            }

            ~FileLock() {
                if (handle != -1) {
                    file.close();
                    if (flock(handle, LOCK_UN) == -1) {
                        llvm::errs() << "Failed to unlock file: " << path << "\n";
                    }
                    close(handle);
                }
            }

        #endif

        operator bool() const {
            return static_cast<bool>(file);
        }

        template <typename T>
        FileLock& operator<<(const T& line) {
            file << line << '\n';
            return *this;
        }

        FileLock& operator>>(std::string& line) {
            std::getline(file, line);
            return *this;
        }

        std::fstream* operator->() {
            return &file;
        }

    };

    std::string get_source_path(const clang::CompilerInstance& compiler) {
        auto& src_mgr = compiler.getSourceManager();
        auto main_file = src_mgr.getFileEntryRefForID(
            src_mgr.getMainFileID()
        );
        return main_file ? main_file->getName().str() : "";
    }

}


/////////////////////////
////    CONSUMERS    ////
/////////////////////////


/* ASTConsumers are responsible for triggering the AST visitors at some point during
 * the build process.
 *
 * Overriding HandleTranslationUnit will parse the complete AST for an entire
 * translation unit after it has been constructed, and will typically use a 
 * RecursiveASTVisitor to do so.  This triggers an extra pass over the AST, so it's a
 * bit more expensive as a result.
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
    std::string module_path;
    std::string import_name;
    std::string export_name;
    std::string binding_path;
    std::string cache_path;
    std::string body;  // TODO: replace this with vectors of wrapper classes

    /* Check if a declaration is marked with [[py::noexport]]. */
    bool noexport(const clang::NamedDecl& decl) {
        for (auto&& attr : decl.getAttrs()) {
            if (
                attr->getKind() == clang::attr::Annotate &&
                static_cast<clang::AnnotateAttr*>(attr)->getAnnotation() == "noexport"
            ) {
                return true;
            }
        }
        return false;
    }

    /* Emit a binding for an exported C++ function. */
    void emit(const clang::FunctionDecl& func) {
        std::string name = func.getNameAsString();
        std::string qualname = func.getQualifiedNameAsString();  // includes namespaces

        llvm::errs() << "EMITTING FUNCTION " << qualname << "\n";

        // TODO: generate a lambda with python argument annotations to enable keyword
        // arguments, etc.

        body += "    m.def(\"" + name + "\", \"\", " + qualname + ");\n";
    }

    /* Write the final binding file to disk if it has changed. */
    void write() {
        impl::FileLock file(binding_path);
        if (!file) {
            llvm::errs() << "failed to open file: " << binding_path << "\n";
            return;
        }
        std::stringstream buffer;
        buffer << file->rdbuf();
        std::string contents = buffer.str();
        if ((body + "\n") == contents) {
            llvm::errs() << "no changes to file: " << binding_path << "\n";
            return;
        }
        file->close();
        file->open(binding_path, std::ios::out | std::ios::trunc);
        file << body;
    }

    /* Update the AST cache to avoid re-parsing the same file. */
    void update_cache() {
        impl::FileLock cache_file(cache_path);
        if (!cache_file) {
            llvm::errs() << "failed to open cache file: " << cache_path << "\n";
            return;
        }
        auto cache = json::parse(cache_file.file);
        cache[impl::get_source_path(compiler)]["parsed"] = binding_path;
        cache_file->close();
        cache_file->open(cache_path, std::ios::out | std::ios::trunc);
        cache_file << cache.dump(4);
    }

public:

    ExportConsumer(
        clang::CompilerInstance& compiler,
        std::string module_path,
        std::string import_name,
        std::string export_name,
        std::string binding_path,
        std::string cache_path
    ) : compiler(compiler), module_path(module_path), import_name(import_name),
        export_name(export_name), binding_path(binding_path), cache_path(cache_path)
    {
        body += "#include <bertrand/python.h>\n";
        body += "import " + import_name + ";\n";
        body += "\n";
        body += "\n";
        body += "BERTRAND_MODULE(" + export_name + ", m) {\n";
    }

    ~ExportConsumer() {
        body += "}";
        write();
        update_cache();
    }

    /* Generate a binding for every top-level declaration marked with the `export`
    keyword that is found during the initial parse, as the source is processed. */
    bool HandleTopLevelDecl(clang::DeclGroupRef decl_group) override {
        using llvm::dyn_cast;
        using clang::Decl;
        using clang::ExportDecl;
        using clang::FunctionDecl;
        using clang::CXXRecordDecl;
        using clang::VarDecl;
        using clang::NamespaceDecl;
        using clang::UsingDecl;

        for (const Decl* _decl : decl_group) {
            if (const ExportDecl* decl = dyn_cast<ExportDecl>(_decl)) {
                for (const Decl* subdecl : decl->decls()) {
                    if (const FunctionDecl* func = dyn_cast<FunctionDecl>(subdecl)) {
                        if (noexport(*func)) {
                            continue;
                        }
                        emit(*func);
                        llvm::errs() << "exported function " << func->getNameAsString() << "\n";

                    } else if (const CXXRecordDecl* type = dyn_cast<CXXRecordDecl>(subdecl)) {
                        if (noexport(*type)) {
                            continue;
                        }
                        // emit(*type);
                        llvm::errs() << "exported type " << type->getNameAsString() << "\n";

                    } else if (const VarDecl* var = dyn_cast<VarDecl>(subdecl)) {
                        if (noexport(*var)) {
                            continue;
                        }
                        // emit(*var);
                        llvm::errs() << "exported variable " << var->getNameAsString() << "\n";

                    } else if (const NamespaceDecl* name = dyn_cast<NamespaceDecl>(subdecl)) {
                        if (noexport(*name)) {
                            continue;
                        }
                        // emit(*name);
                        llvm::errs() << "exported namespace " << name->getNameAsString() << "\n";

                    // } else if (const UsingDecl* use = dyn_cast<UsingDecl>(subdecl)) {
                    //     if (noexport(*use)) {
                    //         continue;
                    //     }
                    //     // emit(*use);
                    //     llvm::errs() << "exported using " << name->getNameAsString() << "\n";

                    } else {
                        llvm::errs() << "unhandled export " << subdecl->getDeclKindName() << "\n";
                    }
                }
            }
        }

        return true;
    }

};


class MainConsumer : public clang::ASTConsumer {
    clang::CompilerInstance& compiler;
    std::string cache_path;

public:

    MainConsumer(
        clang::CompilerInstance& compiler,
        std::string cache_path
    ) : compiler(compiler), cache_path(cache_path) {}

    bool HandleTopLevelDecl(clang::DeclGroupRef decl_group) override {
        for (const clang::Decl* decl : decl_group) {
            auto* func = llvm::dyn_cast<clang::FunctionDecl>(decl);
            if (func && func->isMain()) {
                // ensure only the file that actually defines main() is considered
                const clang::SourceManager& src_mgr = compiler.getSourceManager();
                auto file_id = src_mgr.getFileID(func->getLocation());
                if (file_id != src_mgr.getMainFileID()) {
                    continue;
                }

                std::string source_path = impl::get_source_path(compiler);
                if (source_path.empty()) {
                    llvm::errs() << "failed to get path for executable\n";
                    return false;
                }

                // open (or create) and lock the cache file within this context
                impl::FileLock file(cache_path);
                if (!file) {
                    llvm::errs() << "failed to open cache file: " << cache_path << "\n";
                    return false;
                }

                auto cache = json::parse(file.file);
                cache[source_path]["executable"] = true;
                llvm::errs() << "found main() in: " << source_path << "\n";
                file->close();
                file->open(cache_path, std::ios::out | std::ios::trunc);
                file << cache.dump(4);
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
    std::string module_path;
    std::string import_name;
    std::string export_name;
    std::string binding_path;
    std::string cache_path;

protected:

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& compiler,
        llvm::StringRef
    ) override {
        impl::FileLock cache_file(cache_path);
        if (!cache_file) {
            llvm::errs() << "failed to open cache file: " << cache_path << "\n";
            return std::make_unique<clang::ASTConsumer>();
        }

        // if the source file is not present in cache, then it is out-of-tree
        std::string source_path = impl::get_source_path(compiler);
        if (source_path.empty()) {
            llvm::errs() << "failed to get path for source of: " << module_path << "\n";
            return std::make_unique<clang::ASTConsumer>();
        }
        auto cache = json::parse(cache_file.file);
        if (!cache.contains(source_path)) {
            llvm::errs() << "skipping out-of-tree module: " << source_path << "\n";
            return std::make_unique<clang::ASTConsumer>();
        }

        // if the "parsed" path is cached, then the module has already been exported
        std::string parsed = cache[source_path]["parsed"];
        if (!parsed.empty()) {
            llvm::errs() << "skipping already-exported module: " << source_path << "\n";
            return std::make_unique<clang::ASTConsumer>();
        }

        // otherwise, parse the AST and write the binding file
        return std::make_unique<ExportConsumer>(
            compiler,
            module_path,
            import_name,
            export_name,
            binding_path,
            cache_path
        );
    }

    bool ParseArgs(
        const clang::CompilerInstance& compiler,
        const std::vector<std::string>& args
    ) override {
        for (const std::string& arg : args) {
            // path to primary module interface unit being examined
            if (arg.starts_with("module=")) {
                module_path = arg.substr(arg.find('=') + 1);

            // module name to import at C++ level
            } else if (arg.starts_with("import=")) {
                import_name = arg.substr(arg.find('=') + 1);

            // module name to export to Python
            } else if (arg.starts_with("export=")) {
                export_name = arg.substr(arg.find('=') + 1);

            // path to output binding file
            } else if (arg.starts_with("binding=")) {
                binding_path = arg.substr(arg.find('=') + 1);

            // path to cache to avoid repeated work
            } else if (arg.starts_with("cache=")) {
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
        return true;
    }

    PluginASTAction::ActionType getActionType() override {
        return AddBeforeMainAction;
    }

};


class MainAction : public clang::PluginASTAction {
    std::string cache_path;

protected:

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& compiler,
        llvm::StringRef
    ) override {
        return std::make_unique<MainConsumer>(compiler, cache_path);
    }

    bool ParseArgs(
        const clang::CompilerInstance& compiler,
        const std::vector<std::string>& args
    ) override {
        for (const std::string& arg : args) {
            if (arg.starts_with("cache=")) {
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
        return true;
    }

    PluginASTAction::ActionType getActionType() override {
        return AddBeforeMainAction;
    }

};


}


/* Register the plugins with the clang driver. */
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
