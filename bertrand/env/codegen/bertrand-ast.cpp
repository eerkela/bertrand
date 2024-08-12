#include <fstream>
#include <ios>
#include <sstream>
#include <string>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/ParseAST.h"
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


/* Get the source path of the translation unit being compiled. */
std::string get_source_path(const clang::CompilerInstance& compiler) {
    auto& src_mgr = compiler.getSourceManager();
    auto main_file = src_mgr.getFileEntryRefForID(
        src_mgr.getMainFileID()
    );
    return main_file ? main_file->getName().str() : "";
}


/* Extract the literal text from a source range. */
std::string get_source_text(
    const clang::CompilerInstance& compiler,
    const clang::SourceRange& range
) {
    if (!range.isValid()) {
        return "";
    }
    const clang::SourceManager& src_mgr = compiler.getSourceManager();
    clang::SourceLocation begin = range.getBegin();
    clang::SourceLocation end = clang::Lexer::getLocForEndOfToken(
        range.getEnd(),
        0,
        src_mgr,
        compiler.getLangOpts()
    );
    return std::string(
        src_mgr.getCharacterData(begin),
        src_mgr.getCharacterData(end) - src_mgr.getCharacterData(begin)
    );
}


/* Extract the docstring associated with a declaration. */
std::string get_docstring(
    const clang::CompilerInstance& compiler,
    const clang::Decl* decl
) {
    clang::ASTContext& context = compiler.getASTContext();
    const clang::RawComment* comment = context.getRawCommentForAnyRedecl(decl);
    if (!comment) {
        return "";
    }
    return comment->getFormattedText(
        compiler.getSourceManager(),
        compiler.getDiagnostics()
    );
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
 * bit more expensive as a result.  It also requires the implementation of an extra
 * RecursiveASTVisitor class, which is a bit more work.
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

    struct Function {

        struct Param {
            std::string name;
            std::string type;
            std::string default_value;
            bool has_default;

            Param(clang::CompilerInstance& compiler, const clang::ParmVarDecl* decl) :
                name(decl->getNameAsString()),
                type(decl->getType().getAsString()),
                has_default(decl->hasDefaultArg())
            {
                if (has_default) {
                    default_value = get_source_text(
                        compiler,
                        decl->getDefaultArgRange()
                    );
                }
            }

        };

        std::string name;
        std::string qualname;
        std::string docstring;
        std::vector<Param> params;
        std::string return_type;
        bool is_variadic;

        // TODO: handle forward declarations somehow

        Function(clang::CompilerInstance& compiler, const clang::FunctionDecl* decl) :
            name(decl->getNameAsString()),
            qualname(decl->getQualifiedNameAsString()),
            docstring(get_docstring(compiler, decl)),
            return_type(decl->getReturnType().getAsString()),
            is_variadic(decl->isVariadic())
        {
            for (const clang::ParmVarDecl* param : decl->parameters()) {
                params.emplace_back(compiler, param);
            }
        }

        // TODO: generate a lambda with python argument annotations to enable keyword
        // arguments, default values, and variadic arguments.

        std::string emit() const {
            llvm::errs() << "exported function " << qualname << "\n";
            for (auto&& param : params) {
                llvm::errs() << "    param: " << param.type << " " << param.name;
                if (param.has_default) {
                    llvm::errs() << " = " << param.default_value << "\n";
                } else {
                    llvm::errs() << "\n";
                }
            }
            llvm::errs() << "    return: " << return_type << "\n";
            llvm::errs() << "    docstring: " << docstring << "\n";

            return "    m.def(\"" + name + "\", \"\", " + qualname + ");\n";
        }

    };

    struct Type {
        std::string name;
        std::string qualname;

        Type(clang::CompilerInstance& compiler, const clang::CXXRecordDecl* decl) :
            name(decl->getNameAsString()), qualname(decl->getQualifiedNameAsString())
        {}

        std::string emit() const {
            llvm::errs() << "exported type " << qualname << "\n";
            return "";
        }

    };

    struct Var {
        std::string name;
        std::string qualname;

        Var(clang::CompilerInstance& compiler, const clang::VarDecl* decl) :
            name(decl->getNameAsString()), qualname(decl->getQualifiedNameAsString())
        {}

        std::string emit() const {
            llvm::errs() << "exported variable " << qualname << "\n";
            return "";
        }

    };

    struct Namespace {
        std::string name;
        std::string qualname;

        Namespace(clang::CompilerInstance& compiler, const clang::NamespaceDecl* decl) :
            name(decl->getNameAsString()), qualname(decl->getQualifiedNameAsString())
        {}

        std::string emit() const {
            llvm::errs() << "exported namespace " << qualname << "\n";
            return "";
        }

    };

    std::vector<Function> functions;
    std::vector<Type> types;
    std::vector<Var> vars;
    std::vector<Namespace> namespaces;

    bool noexport(const clang::NamedDecl* decl) {
        for (auto&& attr : decl->getAttrs()) {
            if (
                attr->getKind() == clang::attr::Annotate &&
                static_cast<clang::AnnotateAttr*>(attr)->getAnnotation() == "noexport"
            ) {
                return true;
            }
        }
        return false;
    }

    std::string header() {
        std::string header;
        header += "#include <bertrand/python.h>\n";
        header += "import " + import_name + ";\n";
        header += "\n";
        header += "\n";
        header += "BERTRAND_MODULE(" + export_name + ", m) {\n";
        return header;
    }

    std::string footer() {
        return "}\n";
    }

    void write(const std::string& body) {
        FileLock file(binding_path);
        if (!file) {
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            unsigned diagnostics_id = diagnostics.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "failed to open file: %0"
            );
            diagnostics.Report(diagnostics_id) << binding_path;
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

    void update_cache() {
        FileLock cache_file(cache_path);
        if (!cache_file) {
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            unsigned diagnostics_id = diagnostics.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "failed to open cache file: %0"
            );
            diagnostics.Report(diagnostics_id) << cache_path;
            return;
        }
        std::string source_path = get_source_path(compiler);
        if (source_path.empty()) {
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            unsigned diagnostics_id = diagnostics.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "failed to get path for source of: "
            );
            diagnostics.Report(diagnostics_id) << module_path;
            return;
        }
        auto cache = json::parse(cache_file.file);
        cache[source_path]["parsed"] = binding_path;
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
        // TODO: clear the binding file if it exists and insert only the export module
        // partition and the import bertrand.python dependency.
    }

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
                        if (!noexport(func)) {
                            functions.emplace_back(compiler, func);
                        }

                    } else if (
                        const CXXRecordDecl* type = dyn_cast<CXXRecordDecl>(subdecl)
                    ) {
                        if (!noexport(type)) {
                            types.emplace_back(compiler, type);
                        }

                    } else if (
                        const VarDecl* var = dyn_cast<VarDecl>(subdecl)
                    ) {
                        if (!noexport(var)) {
                            vars.emplace_back(compiler, var);
                        }

                    } else if (
                        const NamespaceDecl* name = dyn_cast<NamespaceDecl>(subdecl)
                    ) {
                        if (!noexport(name)) {
                            namespaces.emplace_back(compiler, name);
                        }

                    // } else if (
                    //     const UsingDecl* use = dyn_cast<UsingDecl>(subdecl)
                    //) {
                    //     if (!noexport(use)) {
                    //         usings.emplace_back(compiler, use);
                    //     }

                    } else {
                        llvm::errs() << "unhandled export " << subdecl->getDeclKindName() << "\n";
                    }
                }
            }
        }

        return true;
    }

    void HandleTranslationUnit(clang::ASTContext& context) override {
        // write the bindings to the previously-blank :bertrand partition
        std::string body = header();
        for (const Namespace& name : namespaces) {
            body += name.emit();
        }
        for (const Type& type : types) {
            body += type.emit();
        }
        for (const Var& var : vars) {
            body += var.emit();
        }
        for (const Function& func : functions) {
            body += func.emit();
        }
        body += footer();
        write(body);
        update_cache();

        clang::FileManager& file_mgr = compiler.getFileManager();
        clang::SourceManager& src_mgr = compiler.getSourceManager();
        clang::Preprocessor& preprocessor = compiler.getPreprocessor();
        clang::ASTConsumer& consumer = compiler.getASTConsumer();

        // extend the AST with the newly-written bindings
        auto file = file_mgr.getFile(binding_path);
        if (!file) {
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            unsigned diagnostics_id = diagnostics.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "failed to open :bertrand partition at %0"
            );
            diagnostics.Report(diagnostics_id) << binding_path;
            return;
        }
        clang::FileID file_id = src_mgr.translateFile(file.get());
        if (file_id.isInvalid()) {
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            unsigned diagnostics_id = diagnostics.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "failed to get file ID for :bertrand partition at %0"
            );
            diagnostics.Report(diagnostics_id) << binding_path;
            return;
        }
        preprocessor.EnterSourceFile(
            file_id,
            nullptr,
            src_mgr.getLocForStartOfFile(file_id)
        );
        clang::ParseAST(preprocessor, &consumer, compiler.getASTContext());
    }

};


class MainConsumer : public clang::ASTConsumer {
    clang::CompilerInstance& compiler;
    std::string cache_path;

public:

    MainConsumer(clang::CompilerInstance& compiler, std::string cache_path) :
        compiler(compiler), cache_path(cache_path)
    {}

    bool HandleTopLevelDecl(clang::DeclGroupRef decl_group) override {
        for (const clang::Decl* decl : decl_group) {
            auto* func = llvm::dyn_cast<clang::FunctionDecl>(decl);
            if (func && func->isMain()) {
                // ensure that imported main() functions are not considered
                const clang::SourceManager& src_mgr = compiler.getSourceManager();
                auto file_id = src_mgr.getFileID(func->getLocation());
                if (file_id != src_mgr.getMainFileID()) {
                    continue;
                }

                std::string source_path = get_source_path(compiler);
                if (source_path.empty()) {
                    clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
                    unsigned diagnostics_id = diagnostics.getCustomDiagID(
                        clang::DiagnosticsEngine::Error,
                        "failed to get path for executable"
                    );
                    diagnostics.Report(diagnostics_id);
                    return false;
                }

                // open (or create) and lock the cache file within this context
                FileLock file(cache_path);
                if (!file) {
                    clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
                    unsigned diagnostics_id = diagnostics.getCustomDiagID(
                        clang::DiagnosticsEngine::Error,
                        "failed to open cache file: %0"
                    );
                    diagnostics.Report(diagnostics_id) << cache_path;
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
    bool run;

protected:

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& compiler,
        llvm::StringRef
    ) override {
        if (!run) {
            return std::make_unique<clang::ASTConsumer>();
        }
        FileLock cache_file(cache_path);
        if (!cache_file) {
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            unsigned diagnostics_id = diagnostics.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "failed to open cache file: %0"
            );
            diagnostics.Report(diagnostics_id) << cache_path;
            return std::make_unique<clang::ASTConsumer>();
        }

        // if the source file is not present in cache, then it is out-of-tree
        std::string source_path = get_source_path(compiler);
        if (source_path.empty()) {
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            unsigned diagnostics_id = diagnostics.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "failed to get path for source of: "
            );
            diagnostics.Report(diagnostics_id) << module_path;
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
            if (arg == "run") {
                run = true;

            // path to primary module interface unit being examined
            } else if (arg.starts_with("module=")) {
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

        if (run && (
            module_path.empty() || import_name.empty() || export_name.empty() ||
            binding_path.empty() || cache_path.empty()
        )) {
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            unsigned diagnostics_id = diagnostics.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "missing required argument '%0'"
            );
            if (module_path.empty()) {
                diagnostics.Report(diagnostics_id) << "module";
            }
            if (import_name.empty()) {
                diagnostics.Report(diagnostics_id) << "import";
            }
            if (export_name.empty()) {
                diagnostics.Report(diagnostics_id) << "export";
            }
            if (binding_path.empty()) {
                diagnostics.Report(diagnostics_id) << "binding";
            }
            if (cache_path.empty()) {
                diagnostics.Report(diagnostics_id) << "cache";
            }
            return false;
        }

        return true;
    }

    PluginASTAction::ActionType getActionType() override {
        return AddBeforeMainAction;
    }

};


class MainAction : public clang::PluginASTAction {
    std::string cache_path;
    bool run;

protected:

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& compiler,
        llvm::StringRef
    ) override {
        if (!run) {
            return std::make_unique<clang::ASTConsumer>();
        }
        return std::make_unique<MainConsumer>(compiler, cache_path);
    }

    bool ParseArgs(
        const clang::CompilerInstance& compiler,
        const std::vector<std::string>& args
    ) override {
        for (const std::string& arg : args) {
            if (arg == "run") {
                run = true;

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

        if (run && cache_path.empty()) {
            clang::DiagnosticsEngine& diagnostics = compiler.getDiagnostics();
            unsigned diagnostics_id = diagnostics.getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "missing required argument '%0'"
            );
            diagnostics.Report(diagnostics_id) << "cache";
            return false;
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
