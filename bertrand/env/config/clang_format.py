"""TODO"""
from __future__ import annotations

from typing import Annotated, Any, Literal, Self

from pydantic import (
    AfterValidator,
    BaseModel,
    ConfigDict,
    Field,
    NonNegativeInt,
    model_validator,
)

from .core import (
    Config,
    NoWhiteSpace,
    RegexPattern,
    Resource,
    dump_yaml,
    resource,
)
from ..run import CONTAINER_TMP_MOUNT, atomic_write_text


# TODO: I should maybe also write a `.clang-format-ignore` file as part of this.
# There are actually many integrations described in
# https://clang.llvm.org/docs/ClangFormat.html

# TODO: it's also possible to toggle clang-format on/off in files by using
# // clang-format off/on comments


@resource("clang-format")
class ClangFormat(Resource):
    """A resource describing a `.clang-format` file, which is used to configure
    clang-format for C++ code formatting.  The `[tool.clang-format]` table is
    projected directly to YAML with no key remapping.
    """
    # pylint: disable=missing-function-docstring, unused-argument, missing-return-doc

    class Model(BaseModel):
        """Validate the `[clang-format]` table.

        NOTE: These are opinionated defaults designed to make C++ code as recognizable
        as possible to Python developers.  If your project has an existing style guide,
        or you prefer a more traditional C++ style, feel free to disable automatic
        formatting or modify these settings as needed.
        """
        model_config = ConfigDict(extra="forbid")
        Enable: Annotated[bool, Field(
            default=False,
            description="Whether to enable clang-format auto formatting.",
        )]
        AccessModifierOffset: Annotated[NonNegativeInt, Field(
            default=0,
            description=
                "Indentation offset for access modifiers (e.g. `public`, `private`, "
                "`protected`).  This is added to the normal indentation level of the "
                "line.  For example, an offset of 1 would indent access modifiers one "
                "additional level compared to other code, while an offset of -1 would "
                "outdent them by one level.",
        )]
        AlwaysBreakBeforeMultilineStrings: Annotated[bool, Field(
            default=True,
            description=
                "Whether to always break before multiline string literals.\n"
                "   `true`:\n"
                "       ```cpp\n"
                "       aaaa =\n"
                "           \"bbbb\"\n"
                "           \"cccc\";\n"
                "       ```\n"
                "   `false`:\n"
                "       ```cpp\n"
                "       aaaa = \"bbbb\"\n"
                "              \"cccc\";\n"
                "       ```",
        )]
        AttributeMacros: Annotated[list[NoWhiteSpace], Field(
            default_factory=list,
            examples=["[\"__declspec\"]"],
            description=
                "List of names to treat as custom language attributes, similar to "
                "`[[nodiscard]]`, etc.",
        )]
        BinPackArguments: Annotated[bool, Field(
            default=False,
            description=
                "Control whether to bin-pack function call arguments.\n"
                "   `true`:\n"
                "       ```cpp\n"
                "       void f() {\n"
                "           f(aaaaaaaaaaaaaaaaaaaa, aaaaaaaaaaaaaaaaaaaa,\n"
                "             aaaaaaaaaaaaaaaaaaaa);\n"
                "       }\n"
                "       ```\n"
                "   `false`:\n"
                "       ```cpp\n"
                "       void f() {\n"
                "           f(aaaaaaaaaaaaaaaaaaaa,\n"
                "             aaaaaaaaaaaaaaaaaaaa,\n"
                "             aaaaaaaaaaaaaaaaaaaa);\n"
                "       }\n"
                "       ```",
        )]
        BinPackLongBracedList: Annotated[bool, Field(
            default=True,
            description=
                "Override 'BinPackArguments=false' for excessively long braced lists, "
                "which can help compress array definitions, for example.",
        )]
        BinPackParameters: Annotated[Literal["BinPack", "OnePerLine", "AlwaysOnePerLine"], Field(
            default="OnePerLine",
            examples=["BinPack", "OnePerLine", "AlwaysOnePerLine"],
            description=
                "Control how to format function parameters.\n"
                "   `BinPack`:\n"
                "       ```cpp\n"
                "       void f(int a, int bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb,\n"
                "              int ccccccccccccccccccccccccccccccccccccccccccc);\n"
                "       ```\n"
                "   `OnePerLine`:\n"
                "       ```cpp\n"
                "       if all parameters fit on one line:\n"
                "           void f(int a, int b, int c);\n"
                "       else:\n"
                "           void f(int a,\n"
                "                  int b,\n"
                "                  int ccccccccccccccccccccccccccccccccccccc);\n"
                "       ```\n"
                "   `AlwaysOnePerLine`:\n"
                "       ```cpp\n"
                "       void f(int a,\n"
                "              int b,\n"
                "              int c);\n"
                "       ```",
        )]
        BitFieldColonSpacing: Annotated[Literal["None", "Before", "After", "Both"], Field(
            default="Both",
            examples=["None", "Before", "After", "Both"],
            description=
                "Control the spacing around the colon in C++ bitfield declarations.\n"
                "   `None`:\n"
                "       ```cpp\n"
                "       unsigned bf:2;\n"
                "       ```\n"
                "   `Before`:\n"
                "       ```cpp\n"
                "       unsigned bf :2;\n"
                "       ```\n"
                "   `After`:\n"
                "       ```cpp\n"
                "       unsigned bf: 2;\n"
                "       ```\n"
                "   `Both`:\n"
                "       ```cpp\n"
                "       unsigned bf : 2;\n"
                "       ```",
        )]
        ColumnLimit: Annotated[NonNegativeInt, Field(
            default=88,
            description=
                "The column beyond which clang-format will try to wrap lines.  This "
                "is not a hard limit, and clang-format may exceed it if necessary to "
                "avoid breaking the code in undesirable ways, but it serves as a "
                "guideline for how aggressively to break lines.",
        )]
        CompactNamespaces: Annotated[bool, Field(
            default=False,
            description=
                "If true, consecutive namespace declarations will be on the same "
                "line.  If false, each namespace is declared on a new line.\n"
                "   `true`:\n"
                "       ```cpp\n"
                "       namespace Foo { namespace Bar {\n"
                "       }}\n"
                "       ```\n"
                "   `false`:\n"
                "       ```cpp\n"
                "       namespace Foo {\n"
                "       namespace Bar {\n"
                "       }\n"
                "       }\n"
                "       ```",
        )]
        EmptyLineAfterAccessModifier: Annotated[Literal["Never", "Leave", "Always"], Field(
            default="Leave",
            examples=["Never", "Leave", "Always"],
            description=
                "Control whether to insert an empty line after access modifiers "
                "(e.g. `public`, `private`, `protected`):\n"
                "   `Never`: remove all empty lines after access modifiers\n"
                "       ```cpp\n"
                "       struct foo {\n"
                "       private:\n"
                "           int i;\n"
                "       protected:\n"
                "           int j;\n"
                "           /* comment */\n"
                "       public:\n"
                "           foo() {}\n"
                "       private:\n"
                "       protected:\n"
                "       };\n"
                "       ```\n"
                "   `Leave`: preserve user formatting\n"
                "   `Always`: always add empty line after access modifiers if there are "
                "none\n"
                "       ```cpp\n"
                "       struct foo {\n"
                "       private:\n"
                "\n"
                "           int i;\n"
                "       protected:\n"
                "\n"
                "           int j;\n"
                "           /* comment */\n"
                "       public:\n"
                "\n"
                "           foo() {}\n"
                "       private:\n"
                "\n"
                "       protected:\n"
                "\n"
                "       };\n"
                "       ```",
        )]
        EmptyLineBeforeAccessModifier: Annotated[
            Literal["Never", "Leave", "LogicalBlock", "Always"],
            Field(
                default="Leave",
                examples=["Never", "Leave", "LogicalBlock", "Always"],
                description=
                    "Control whether to insert an empty line before access modifiers "
                    "(e.g. `public`, `private`, `protected`):\n"
                    "   `Never`: remove all empty lines before access modifiers\n"
                    "       ```cpp\n"
                    "       struct foo {\n"
                    "       private:\n"
                    "           int i;\n"
                    "       protected:\n"
                    "           int j;\n"
                    "           /* comment */\n"
                    "       public:\n"
                    "           foo() {}\n"
                    "       private:\n"
                    "       protected:\n"
                    "       };\n"
                    "       ```\n"
                    "   `Leave`: preserve user formatting\n"
                    "   `LogicalBlock`: add empty line only when access modifier starts "
                    "a new logical block.\n"
                    "       ```cpp\n"
                    "       struct foo {\n"
                    "       private:\n"
                    "           int i;\n"
                    "\n"
                    "       protected:\n"
                    "           int j;\n"
                    "           /* comment */\n"
                    "       public:\n"
                    "           foo() {}\n"
                    "\n"
                    "       private:\n"
                    "       protected:\n"
                    "       };\n"
                    "       ```\n"
                    "   `Always`:\n"
                    "       ```cpp\n"
                    "       struct foo {\n"
                    "       private:\n"
                    "           int i;\n"
                    "\n"
                    "       protected:\n"
                    "           int j;\n"
                    "           /* comment */\n"
                    "\n"
                    "       public:\n"
                    "           foo() {}\n"
                    "\n"
                    "       private:\n"
                    "\n"
                    "       protected:\n"
                    "       };\n"
                    "       ```",
            )
        ]
        FixNamespaceComments: Annotated[bool, Field(
            default=True,
            description=
                "Add namespace end comments for long namespaces, and fix them if they "
                "are wrong.\n"
                "   `true`:\n"
                "       ```cpp\n"
                "       namespace longNamespace {\n"
                "           void foo();\n"
                "           void bar();\n"
                "       } // namespace longNamespace\n"
                "       namespace shortNamespace { void baz(); }\n"
                "       ```\n"
                "   `false`:\n"
                "       ```cpp\n"
                "       namespace longNamespace {\n"
                "           void foo();\n"
                "           void bar();\n"
                "       }\n"
                "       namespace shortNamespace { void baz(); }\n"
                "       ```",
        )]
        ForEachMacros: Annotated[list[NoWhiteSpace], Field(
            default_factory=list,
            examples=["[\"BOOST_FOREACH\"]"],
            description=
                "List of macros to treat as foreach loops, which affects how their "
                "bodies are formatted.",
        )]
        IfMacros: Annotated[list[NoWhiteSpace], Field(
            default_factory=list,
            examples=["[\"Q_IF\"]"],
            description=
                "List of macros to treat as if statements, which affects how their "
                "bodies are formatted.",
        )]
        IncludeBlocks: Annotated[Literal["Preserve", "Merge"], Field(
            default="Preserve",
            examples=["Preserve", "Merge"],
            description=
                "Control how to format blocks of consecutive #include directives.\n"
                "   `Preserve`: sort each #include block individually\n"
                "       ```cpp\n"
                "       #include \"b.h\"\n          ->      #include \"b.h\"\n"
                "\n"
                "       #include <lib/main.h>               #include \"a.h\"\n"
                "       #include \"a.h\"                    #include <lib/main.h>\n"
                "       ```\n"
                "   `Merge`: merge consecutive #include blocks and sort as one\n"
                "       ```cpp\n"
                "       #include \"a.h\"\n          ->      #include \"a.h\"\n"
                "                                           #include \"b.h\"\n"
                "       #include <lib/main.h>               #include <lib/main.h>\n"
                "       #include \"b.h\"\n"
                "       ```",
        )]
        InsertBraces: Annotated[bool, Field(
            default=True,
            description=
                "Controls whether to insert braces after control statements:\n"
                "   `true`:\n"
                "       ```cpp\n"
                "       if (isa<FunctionDecl>(D)) {\n"
                "           handleFunctionDecl(D);\n"
                "       } else if (isa<VarDecl>(D)) {\n"
                "           handleVarDecl(D);\n"
                "       } else {\n"
                "           return;\n"
                "       }\n"
                "\n"
                "       while (i--) {\n"
                "           for (auto *A : D.attrs()) {\n"
                "               handleAttr(A);\n"
                "           }\n"
                "       }\n"
                "\n"
                "       do {\n"
                "           --i;\n"
                "       } while (i);\n"
                "       ```\n"
                "   `false`:\n"
                "       ```cpp\n"
                "       if (isa<FunctionDecl>(D))\n"
                "           handleFunctionDecl(D);\n"
                "       else if (isa<VarDecl>(D))\n"
                "           handleVarDecl(D);\n"
                "       else\n"
                "           return;\n"
                "\n"
                "       while (i--)\n"
                "           for (auto *A : D.attrs())\n"
                "               handleAttr(A);\n"
                "\n"
                "       do\n"
                "           --i;\n"
                "       while (i);\n"
                "       ```",
        )]
        InsertNewlineAtEOF: Annotated[bool, Field(
            default=True,
            description=
                "Whether to insert a newline at the end of the file if it is missing.",
        )]
        LineEnding: Annotated[Literal["LF", "CRLF", "DeriveLF", "DeriveCRLF"], Field(
            default="DeriveLF",
            description=
                "Line ending style (\\n or \\r\\n) to use:\n"
                "   `LF`: use Unix-style line endings (\\n)\n"
                "   `CRLF`: use Windows-style line endings (\\r\\n)\n"
                "   `DeriveLF`: Use \\n unless the input has more lines ending in \\r\\n.\n"
                "   `DeriveCRLF`: Use \\r\\n unless the input has more lines "
                "ending in \\n.",
        )]
        NamespaceIndentation: Annotated[Literal["None", "Inner", "All"], Field(
            default="None",
            description=
                "The indentation used for namespaces:\n"
                "   `None`: don't indent in namespaces\n"
                "       ````cpp\n"
                "       namespace out {\n"
                "       int i;\n"
                "       namespace in {\n"
                "       int i;\n"
                "       }\n"
                "       }\n"
                "       ```\n"
                "   `Inner`: indent only in inner namespaces (nested in other namespaces)\n"
                "       ```cpp\n"
                "       namespace out {\n"
                "       int i;\n"
                "       namespace in {\n"
                "           int i;\n"
                "       }\n"
                "       }\n"
                "       ```\n"
                "   `All`: indent in all namespaces\n"
                "       ```cpp\n"
                "       namespace out {\n"
                "           int i;\n"
                "           namespace in {\n"
                "               int i;\n"
                "           }\n"
                "       }\n"
                "       ```",
        )]
        NamespaceMacros: Annotated[list[NoWhiteSpace], Field(
            default_factory=list,
            examples=["[\"__LIBCPP_BEGIN_NAMESPACE_STD\"]"],
            description=
                "List of macros to treat as namespace declarations, which affects how "
                "their bodies are formatted.",
        )]
        OneLineFormatOffRegex: Annotated[RegexPattern, Field(
            default="NOFORMAT",
            description=
                "A regex pattern to match in a `//` line comment to disable "
                "clang-format for that line.",
        )]
        PackConstructorInitializers: Annotated[
            Literal["Never", "BinPack", "CurrentLine", "NextLine", "NextLineOnly"],
            Field(
                default="CurrentLine",
                examples=["Never", "BinPack", "CurrentLine", "NextLine", "NextLineOnly"],
                description=
                    "Control how to format constructor initializers:\n"
                    "   `Never`: always put each constructor initializer on its own line\n"
                    "       ```cpp\n"
                    "       Constructor()\n"
                    "           : a(),\n"
                    "             b()\n"
                    "       ```\n"
                    "   `BinPack`: bin-pack constructor initializers\n"
                    "       ```cpp\n"
                    "       Constructor()\n"
                    "           : aaaaaaaaaaaaaaaaaaaa(), bbbbbbbbbbbbbbbbbbbb(),\n"
                    "             cccccccccccccccccccc()\n"
                    "       ```\n"
                    "   `CurrentLine`: put all constructor initializers on the current "
                    "line if they fit. Otherwise, put each one on its own line.\n"
                    "       ```cpp\n"
                    "       Constructor() : a(), b(), c()\n"
                    "       Constructor()\n"
                    "           : aaaaaaaaaaaaaaaaaaaa(),\n"
                    "             bbbbbbbbbbbbbbbbbbbb()\n"
                    "             cccccccccccccccccccc()\n"
                    "       ```\n"
                    "   `NextLine`: same as `CurrentLine., except that if all "
                    "constructor initializers do not fit on the current line, try to "
                    "fit them on the next line.\n"
                    "       ```cpp\n"
                    "       Constructor() : a(), b(), c()\n"
                    "       Constructor()\n"
                    "           : a(), bbbbbbbbbbbbbbbbbbbb(), cccccccccccccccccccc()"
                    "       Constructor()\n"
                    "           : aaaaaaaaaaaaaaaaaaaa(),\n"
                    "             bbbbbbbbbbbbbbbbbbbb()\n"
                    "             cccccccccccccccccccc()\n"
                    "       ```\n"
                    "   `NextLineOnly`: put all constructor initializers on the next "
                    "line if they fit. Otherwise, put each one on its own line.\n"
                    "       ```cpp\n"
                    "       Constructor()\n"
                    "           : a(), b(), c()\n"
                    "       Constructor()\n"
                    "           : a(), bbbbbbbbbbbbbbbbbbbb(), cccccccccccccccccccc()"
                    "       Constructor()\n"
                    "           : aaaaaaaaaaaaaaaaaaaa(),\n"
                    "             bbbbbbbbbbbbbbbbbbbb()\n"
                    "             cccccccccccccccccccc()\n"
                    "       ```",
            )
        ]
        PointerAlignment: Annotated[Literal["Left", "Right", "Middle"], Field(
            default="Left",
            examples=["Left", "Right", "Middle"],
            description=
                "Pointer alignment style:\n"
                "   `Left`: align pointers to the left.\n"
                "       ```cpp\n"
                "       int* a;\n"
                "       ```\n"
                "   `Right`: align pointers to the right.\n"
                "       ```cpp\n"
                "       int *a;\n"
                "       ```\n"
                "   `Middle`: align pointers in the middle.\n"
                "       ```cpp\n"
                "       int * a;\n"
                "       ```",
        )]

        @staticmethod
        def _check_qualifier_order(value: list[str]) -> list[str]:
            seen: set[str] = set()
            for qualifier in value:
                if qualifier in seen:
                    raise ValueError(
                        "duplicate qualifier in ClangFormat.QualifierOrder: "
                        f"'{qualifier}'"
                    )
                seen.add(qualifier)
            if "type" not in seen:
                raise ValueError(
                    "ClangFormat.QualifierOrder must include a 'type' qualifier"
                )
            return value

        QualifierOrder: Annotated[
            list[Literal[
                "inline", "static", "constexpr", "friend", "const", "volatile",
                "restrict", "type"
            ]],
            AfterValidator(_check_qualifier_order),
            Field(
                default_factory=lambda: [
                    "inline", "static", "constexpr", "friend", "const", "volatile",
                    "restrict", "type"
                ],
                examples=[
                    "inline", "static", "constexpr", "friend", "const", "volatile",
                    "restrict", "type"
                ],
                description=
                    "The canonical order in which to format qualifiers.  Must include "
                    "each of the following qualifiers exactly once: `inline`, "
                    "`static`, `constexpr`, `friend`, `const`, `volatile`, "
                    "`restrict`, and `type` (for the type itself).  With the default"
                    "order:\n"
                    "   ````cpp\n"
                    "   friend static inline const int* foo();\n"
                    "   ```\n"
                    "would format to:\n"
                    "   ```cpp\n"
                    "   inline static friend const int* foo();\n"
                    "   ```",
            )
        ]
        ReferenceAlignment: Annotated[Literal["Left", "Right", "Middle"], Field(
            default="Left",
            examples=["Left", "Right", "Middle"],
            description=
                "Reference alignment style:\n"
                "   `Left`: align references to the left.\n"
                "       ```cpp\n"
                "       int& a = b;\n"
                "       ```\n"
                "   `Right`: align references to the right.\n"
                "       ```cpp\n"
                "       int &a = b;\n"
                "       ```\n"
                "   `Middle`: align references in the middle.\n"
                "       ```cpp\n"
                "       int & a = b;\n"
                "       ```",
        )]
        ReflowComments: Annotated[Literal["Never", "IndentOnly", "Always"], Field(
            default="Always",
            examples=["Never", "IndentOnly", "Always"],
            description=
                "Control automatic line wrapping for comments:\n"
                "   `Never`: never reflow comments\n"
                "       ```cpp\n"
                "       // veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment "
                "with plenty of information\n"
                "       /* second veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment "
                "with plenty of information */\n"
                "       /* third veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment "
                "with plenty of information\n"
                "           * and a misaligned second line */\n"
                "       ```\n"
                "   `IndentOnly`: only apply indentation rules, moving comments left or "
                "right, without changing formatting inside the comments\n"
                "       ```cpp\n"
                "       // veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment "
                "with plenty of information\n"
                "       /* second veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment "
                "with plenty of information */\n"
                "       /* third veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment "
                "with plenty of information\n"
                "        * and a misaligned second line */\n"
                "       ```\n"
                "   `Always`: apply indentation rules and reflow long comments into new "
                "lines, trying to obey the ColumnLimit.\n"
                "       ```cpp\n"
                "       // veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment "
                "with plenty of\n"
                "       // information\n"
                "       /* second veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment "
                "with plenty of\n"
                "        * information */\n"
                "       /* third veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongComment "
                "with plenty of\n"
                "        * information and a misaligned second line */\n"
                "       ```",
        )]
        RemoveEmptyLinesInUnwrappedLines: Annotated[bool, Field(
            default=True,
            description=
                "Controls whether to remove empty lines within unwrapped lines:\n"
                "   `true`:\n"
                "       ```cpp\n"
                "       int c = a + b;\n"
                "\n"
                "       enum : unsigned {\n"
                "           AA = 0,\n"
                "           BB\n"
                "       } myEnum;\n"
                "\n"
                "       while (true) {\n"
                "       }\n"
                "       ```\n"
                "   `false`:\n"
                "       ```cpp\n"
                "       int c\n"
                "\n"
                "           = a + b;\n"
                "\n"
                "       enum : unsigned\n"
                "\n"
                "       {\n"
                "           AA = 0,\n"
                "           BB\n"
                "       } myEnum;\n"
                "\n"
                "       while (\n"
                "\n"
                "           true) {\n"
                "       }\n"
                "       ```",
        )]
        RequiresClausePosition: Annotated[
            Literal[
                "OwnLine", "OwnLineWithBrace", "WithPreceding", "WithFollowing",
                "SingleLine"
            ],
            Field(
                default="WithPreceding",
                examples=[
                    "OwnLine", "OwnLineWithBrace", "WithPreceding", "WithFollowing",
                    "SingleLine"
                ],
                description=
                    "Controls the position of the `requires` clause in C++20 template "
                    "constraints:\n"
                    "   `OwnLine`: always put the requires clause on its own line "
                    "(possibly followed by a semicolon).\n"
                    "       ```cpp\n"
                    "       template <typename T>\n"
                    "           requires C<T>\n"
                    "       struct Foo {...};\n"
                    "\n"
                    "       template <typename T>\n"
                    "       void bar(T t)\n"
                    "           requires C<T>;\n"
                    "\n"
                    "       template <typename T>\n"
                    "           requires C<T>\n"
                    "       void bar(T t) {...}\n"
                    "\n"
                    "       template <typename T>\n"
                    "       void baz(T t)\n"
                    "           requires C<T>\n"
                    "       {...}\n"
                    "       ```\n"
                    "   `OwnLineWithBrace`: as with 'OwnLine', except, unless "
                    "otherwise prohibited, place a following open brace (of a "
                    "function definition) to follow on the same line.\n"
                    "       ```cpp\n"
                    "       void bar(T t)\n"
                    "           requires C<T> {\n"
                    "           return;\n"
                    "       }\n"
                    "\n"
                    "       void bar(T t)\n"
                    "           requires C<T> {}\n"
                    "\n"
                    "       template <typename T>\n"
                    "           requires C<T>\n"
                    "       void baz(T t) {...}\n"
                    "       ```\n"
                    "   `WithPreceding`: try to put the clause together with the "
                    "preceding part of a declaration. For class templates: stick to "
                    "the template declaration. For function templates: stick to the "
                    "template declaration. For function declaration followed by a "
                    "requires clause: stick to the parameter list.\n"
                    "       ```cpp\n"
                    "       template <typename T> requires C<T>\n"
                    "       struct Foo {...};\n"
                    "\n"
                    "       template <typename T> requires C<T>\n"
                    "       void bar(T t) {...}"
                    "\n"
                    "       template <typename T>\n"
                    "       void baz(T t) requires C<T>\n"
                    "       {...}\n"
                    "       ```\n"
                    "   `WithFollowing`: try to put the requires clause together with "
                    "the class or function declaration.\n"
                    "       ```cpp\n"
                    "       template <typename T>\n"
                    "       requires C<T> struct Foo {...};\n"
                    "\n"
                    "       template <typename T>\n"
                    "       requires C<T> void bar(T t) {...}\n"
                    "\n"
                    "       template <typename T>\n"
                    "       void baz(T t)\n"
                    "       requires C<T> {...}\n"
                    "       ```\n"
                    "   `SingleLine`: try to put everything in the same line if "
                    "possible.  Otherwise normal line breaking rules take over.\n"
                    "       ```cpp\n"
                    "       // Fitting:\n"
                    "       template <typename T> requires C<T> struct Foo {...};\n"
                    "       template <typename T> requires C<T> void bar(T t) {...}\n"
                    "       template <typename T> void bar(T t) requires C<T> {...}\n"
                    "\n"
                    "       // Not fitting, one possible example:\n"
                    "       template <typename LongName>\n"
                    "       requires C<LongName>\n"
                    "       struct Foo {...};\n"
                    "\n"
                    "       template <typename LongName>\n"
                    "       requires C<LongName>\n"
                    "       void bar(LongName ln) {...}\n"
                    "\n"
                    "       template <typename LongName>\n"
                    "       void bar(LongName ln)\n"
                    "           requires C<LongName> {...}\n"
                    "       ```",
            )
        ]
        RequiresExpressionIndentation: Annotated[Literal["OuterScope", "Keyword"], Field(
            default="OuterScope",
            examples=["OuterScope", "Keyword"],
            description=
                "Controls the indentation used for requires expression bodies:\n"
                "   `OuterScope`: align requires expression body relative to the "
                "indentation level of the outer scope the requires expression resides "
                "in.\n"
                "       ```cpp\n"
                "       template <typename T>\n"
                "       concept C = requires(T t) {\n"
                "           ...\n"
                "       }\n"
                "       ```\n"
                "   `Keyword`: align requires expression body relative to the requires "
                "keyword.\n"
                "       ```cpp\n"
                "       template <typename T>\n"
                "       concept C = requires(T t) {\n"
                "                       ...\n"
                "                   }\n"
                "       ```",
        )]
        SeparateDefinitionBlocks: Annotated[Literal["Never", "Leave", "Always"], Field(
            default="Leave",
            examples=["Never", "Leave", "Always"],
            description=
                "Controls how empty lines are used to separate definition blocks, "
                "including classes, structs, enums, and functions:\n"
                "   `Never`:\n"
                "       ```cpp\n"
                "       #include <cstring>\n"
                "       struct Foo {\n"
                "           int a, b, c;\n"
                "       };\n"
                "       namespace Ns {\n"
                "       class Bar {\n"
                "       public:\n"
                "           struct Foobar {\n"
                "               int a;\n"
                "               int b;\n"
                "           };\n"
                "       private:\n"
                "           int t;\n"
                "           int method1() {\n"
                "               // ...\n"
                "           }\n"
                "           enum List {\n"
                "               ITEM1,\n"
                "               ITEM2\n"
                "           };\n"
                "           template<typename T>\n"
                "           int method2(T x) {\n"
                "               // ...\n"
                "           }\n"
                "           int i, j, k;\n"
                "           int method3(int par) {\n"
                "               // ...\n"
                "           }\n"
                "       };\n"
                "       class C {};\n"
                "       }\n"
                "       ```\n"
                "   `Always`:\n"
                "       ```cpp\n"
                "       #include <cstring>\n"
                "\n"
                "       struct Foo {\n"
                "           int a, b, c;\n"
                "       };\n"
                "\n"
                "       namespace Ns {\n"
                "       class Bar {\n"
                "       public:\n"
                "           struct Foobar {\n"
                "               int a;\n"
                "               int b;\n"
                "           };\n"
                "\n"
                "       private:\n"
                "           int t;\n"
                "\n"
                "           int method1() {\n"
                "               // ...\n"
                "           }\n"
                "\n"
                "           enum List {\n"
                "               ITEM1,\n"
                "               ITEM2\n"
                "           };\n"
                "\n"
                "           template<typename T>\n"
                "           int method2(T x) {\n"
                "               // ...\n"
                "           }\n"
                "\n"
                "           int i, j, k;\n"
                "\n"
                "           int method3(int par) {\n"
                "               // ...\n"
                "           }\n"
                "       };\n"
                "\n"
                "       class C {};\n"
                "       }\n"
                "       ```",
        )]
        ShortNamespaceLines: Annotated[NonNegativeInt, Field(
            default=20,
            description=
                "The maximum number of lines in a namespace definition for it to "
                "be considered a short namespace.  Short namespaces may be "
                "formatted more compactly, possibly without ending comments.",
        )]
        SortUsingDeclarations: Annotated[
            Literal["Never", "Lexicographic", "LexicographicNumeric"],
            Field(
                default="Lexicographic",
                examples=["Never", "Lexicographic", "LexicographicNumeric"],
                description=
                    "Controls whether and how to sort using declarations:\n"
                    "   `Never`: don't sort using declarations\n"
                    "       ```cpp\n"
                    "       using std::chrono::duration_cast;\n"
                    "       using std::move;\n"
                    "       using boost::regex;\n"
                    "       using boost::regex_constants::icase;\n"
                    "       using std::string;\n"
                    "       ```\n"
                    "   `Lexicographic`: Using declarations are sorted in the order "
                    "defined as follows: Split the strings by :: and discard any "
                    "initial empty strings. Sort the lists of names lexicographically, "
                    "and within those groups, names are in case-insensitive "
                    "lexicographic order.\n"
                    "       ```cpp\n"
                    "       using boost::regex;\n"
                    "       using boost::regex_constants::icase;\n"
                    "       using std::chrono::duration_cast;\n"
                    "       using std::move;\n"
                    "       using std::string;\n"
                    "       ```\n"
                    "   `LexicographicNumeric`: using declarations are sorted in the "
                    "order defined as follows: Split the strings by :: and discard any "
                    "initial empty strings. The last element of each list is a "
                    "non-namespace name; all others are namespace names. Sort the "
                    "lists of names lexicographically, where the sort order of "
                    "individual names is that all non-namespace names come before all "
                    "namespace names, and within those groups, names are in "
                    "case-insensitive lexicographic order.\n"
                    "       ```cpp\n"
                    "       using boost::regex;\n"
                    "       using boost::regex_constants::icase;\n"
                    "       using std::move;\n"
                    "       using std::string;\n"
                    "       using std::chrono::duration_cast;\n"
                    "       ```",
            )
        ]
        SpacesBeforeTrailingComments: Annotated[NonNegativeInt, Field(
            default=2,
            description=
                "The minimum number of spaces before trailing line `//` comments on "
                "the same line.",
        )]
        SpacesInAngles: Annotated[Literal["Never", "Leave", "Always"], Field(
            default="Never",
            examples=["Never", "Leave", "Always"],
            description=
                "Controls whether to add spaces between `<`/`>` brackets in "
                "template argument lists:\n"
                "   `Never`: remove spaces after < and before >.\n"
                "       ```cpp\n"
                "       static_cast<int>(arg);\n"
                "       std::function<void(int)> fct;\n"
                "       ```\n"
                "   `Leave`: preserve user formatting\n"
                "   `Always`: always add spaces after < and before >.\n"
                "       ```cpp\n"
                "       static_cast< int >(arg);\n"
                "       std::function< void(int) > fct;\n"
                "       ```",
        )]

        class _SpacesInLineCommentPrefix(BaseModel):
            """Validate the `[tool.clang-format.spaces-in-line-comment-prefix]` table."""
            model_config = ConfigDict(extra="forbid")
            Minimum: Annotated[NonNegativeInt, Field(default=1)]
            Maximum: Annotated[int, Field(default=-1, ge=-1)]

        SpacesInLineCommentPrefix: Annotated[_SpacesInLineCommentPrefix, Field(
            default_factory=_SpacesInLineCommentPrefix.model_construct,
            description=
                "Controls how many spaces are allowed at the start of a `//` line "
                "comment.  To disable the maximum, set it to -1.  This only applies "
                "when 'ReflowComments' is true.",
        )]
        SpacesInSquareBrackets: Annotated[bool, Field(
            default=False,
            description=
                "If true, spaces will be inserted after [ and before ]."
                "   `true`:\n"
                "       ```cpp\n"
                "       int a[ 5 ];\n"
                "       ```\n"
                "   `false`:\n"
                "       ```cpp\n"
                "       int a[5];\n"
                "       ```",
        )]
        TabWidth: Annotated[NonNegativeInt, Field(
            default=4,
            description="The number of columns used for tab stops.",
        )]
        TemplateNames: Annotated[list[NoWhiteSpace], Field(
            default_factory=list,
            examples=["MYTEMPLATE -> MYTEMPLATE<arg> void func() {...}"],
            description=
                "A list of non-keyword identifiers that should be interpreted as "
                "template names.  A < after a template name is annotated as a template "
                "opener instead of a binary operator.",
        )]
        TypeNames: Annotated[list[NoWhiteSpace], Field(
            default_factory=list,
            examples=["MYTYPE -> MYTYPE* var;"],
            description=
                "A list of non-keyword identifiers that should be interpreted as type "
                "names.  A *, &, or && between a type name and another non-keyword "
                "identifier is annotated as a pointer or reference token instead of "
                "a binary operator.",
        )]
        TypenameMacros: Annotated[list[NoWhiteSpace], Field(
            default_factory=list,
            examples=["MYTYPENAME -> MYTYPENAME(typename) var;"],
            description=
                "A list of macros that should be interpreted as type declarations "
                "instead of as function calls.  These are expected to be macros of the "
                "form:\n"
                "   ```cpp\n"
                "   STACK_OF(...)\n"
                "   ```",
        )]
        UseTab: Annotated[
            Literal[
                "Never", "ForIndentation", "ForContinuationAndIndentation",
                "AlignWithSpaces", "Always"
            ],
            Field(
                default="Never",
                examples=[
                    "Never", "ForIndentation", "ForContinuationAndIndentation",
                    "AlignWithSpaces", "Always"
                ],
                description=
                    "Controls if and when to use tab characters in the resulting file:\n"
                    "   `Never`: never use tab.\n"
                    "   `ForIndentation`: use tabs only for indentation.\n"
                    "   `ForContinuationAndIndentation`: fill all leading whitespace "
                    "with tabs, and use spaces for alignment that appears within a "
                    "line (e.g. consecutive assignments and declarations).\n"
                    "   `AlignWithSpaces`: use tabs for line continuation and "
                    "indentation, and spaces for alignment.\n"
                    "   `Always`: use tabs whenever we need to fill whitespace that "
                    "spans at least from one tab stop to the next one.",
            )
        ]
        WrapNamespaceBodyWithEmptyLines: Annotated[Literal["Never", "Leave", "Always"], Field(
            default="Leave",
            examples=["Never", "Leave", "Always"],
            description=
                "Controls whether to wrap the body of a namespace with empty lines:\n"
                "   `Never`: remove all empty lines at the beginning and the end of "
                "namespace body.\n"
                "       ```cpp\n"
                "       namespace N1 {\n"
                "       namespace N2 {\n"
                "       function();\n"
                "       }\n"
                "       }\n"
                "       ```\n"
                "   `Leave`: preserve user formatting.\n"
                "   `Always`: always have at least one empty line at the beginning and "
                "the end of namespace body except that the number of empty lines "
                "between consecutive nested namespace definitions is not increased.\n"
                "       ```cpp\n"
                "       namespace N1 {\n"
                "       namespace N2 {\n"
                "\n"
                "       function();\n"
                "\n"
                "       }\n"
                "       }\n"
                "       ```",
        )]

        class _Align(BaseModel):
            """Validate the `[tool.clang-format.Align]` table."""
            model_config = ConfigDict(extra="forbid")
            AfterOpenBracket: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to align arguments after an open bracket:\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       someLongFunction(argument1,\n"
                    "                        argument2);\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       someLongFunction(argument1,\n"
                    "           argument2);\n"
                    "       ```"
            )]
            ArrayOfStructures: Annotated[Literal["Left", "Right", "None"], Field(
                default="None",
                examples=["Left", "Right", "None"],
                description=
                    "Controls the alignment of array of structure initializers:\n"
                    "   `Left`:\n"
                    "       ```cpp\n"
                    "       struct test demo[] =\n"
                    "       {\n"
                    "           {56, 23,    \"hello\"},\n"
                    "           {-1, 93463, \"world\"},\n"
                    "           {7,  5,     \"!!\"   }\n"
                    "       };\n"
                    "       ```\n"
                    "   `Right`:\n"
                    "       ```cpp\n"
                    "       struct test demo[] =\n"
                    "       {\n"
                    "           {56,    23, \"hello\"},\n"
                    "           {-1, 93463, \"world\"},\n"
                    "           { 7,     5,    \"!!\"}\n"
                    "       };\n"
                    "       ```\n"
                    "   `None`: preserve user formatting."
            )]
            EscapedNewlines: Annotated[
                Literal["DontAlign", "Left", "LeftWithLastLine", "Right"],
                Field(
                    default="Right",
                    examples=["DontAlign", "Left", "LeftWithLastLine", "Right"],
                    description=
                        "Controls the alignment of backslashes in escaped newlines:\n"
                        "   `DontAlign`: don't align escaped newlines\n"
                        "       ```cpp\n"
                        "       #define A \\\n"
                        "           int aaaa; \\\n"
                        "           int b; \\\n"
                        "           int dddddddddd;\n"
                        "       ```"
                        "   `Left`: align escaped newlines as far left as possible.\n"
                        "       ```cpp\n"
                        "       #define A     \\\n"
                        "           int aaaa; \\\n"
                        "           int b;    \\\n"
                        "           int dddddddddd;\n"
                        "       ```"
                        "   `LeftWithLastLine`: align escaped newlines as far left as "
                        "possible, using the last line of the preprocessor directive "
                        "as the reference if it's the longest.\n"
                        "       ```cpp\n"
                        "       #define A           \\\n"
                        "           int aaaa;       \\\n"
                        "           int b;          \\\n"
                        "           int dddddddddd;\n"
                        "       ```"
                        "   `Right`: align escaped newlines to the rightmost position.\n"
                        "       ```cpp\n"
                        "       #define A                                           \\\n"
                        "           int aaaa;                                       \\\n"
                        "           int b;                                          \\\n"
                        "           int dddddddddd;\n"
                        "       ```"
                )
            ]
            Operands: Annotated[Literal["DontAlign", "Align", "AlignAfterOperator"], Field(
                default="Align",
                examples=["DontAlign", "Align", "AlignAfterOperator"],
                description=
                    "Controls the alignment of binary and ternary operators:\n"
                    "   `DontAlign`: don't align operands when wrapping lines.\n"
                    "   `Align`: Horizontally align operands of binary and ternary "
                    "expressions.\n"
                    "       ```cpp\n"
                    "       int aaa = bbbbbbbbbbbbbbb +\n"
                    "                 ccccccccccccccc;\n"
                    "       ```\n"
                    "       If `Break.BeforeBinaryOperators` is set, then the wrapped "
                    "operator is aligned with the operand on the first line.\n"
                    "       ```cpp\n"
                    "       int aaa = bbbbbbbbbbbbbbb\n"
                    "                 + ccccccccccccccc;\n"
                    "   `AlignAfterOperator`: Horizontally align operands of binary "
                    "and ternary expressions.\n"
                    "       ```cpp\n"
                    "       int aaa = bbbbbbbbbbbbbbb\n"
                    "               + ccccccccccccccc;\n"
                    "       ```",
            )]

            class _ConsecutiveAssignments(BaseModel):
                """Validate the `[tool.clang-format.align.ConsecutiveAssignments]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align consecutive assignments:\n"
                        "    `true`:\n"
                        "       ```cpp\n"
                        "       int a            = 1;\n"
                        "       int somelongname = 2;\n"
                        "       double c         = 3;\n"
                        "       ```\n"
                        "    `false`: preserve user formatting.",
                )]
                AcrossEmptyLines: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align across empty lines:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       int a            = 1;\n"
                        "       int somelongname = 2;\n"
                        "       double c         = 3;\n"
                        "\n"
                        "       int d            = 3;\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       int a            = 1;\n"
                        "       int somelongname = 2;\n"
                        "       double c         = 3;\n"
                        "\n"
                        "       int d = 3;\n"
                        "       ```",
                )]
                AcrossComments: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align across comments:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       int d    = 3;\n"
                        "       /* A comment. */\n"
                        "       double e = 4;\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       int d = 3;\n"
                        "       /* A comment. */\n"
                        "       double e = 4;\n"
                        "       ```",
                )]
                AlignCompound: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether compound assignments like `+=` are aligned "
                        "along with `=`:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       a   &= 2;\n"
                        "       bbb  = 2;\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       a &= 2;\n"
                        "       bbb = 2;\n"
                        "       ```",
                )]
                PadOperators: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether short assignment operators are left-padded "
                        "to the same length as long ones in order to put all "
                        "assignment operators to the right of the left hand side.\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       a   >>= 2;\n"
                        "       bbb   = 2;\n"
                        "\n"
                        "       a     = 2;\n"
                        "       bbb >>= 2;\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       a >>= 2;\n"
                        "       bbb = 2;\n"
                        "\n"
                        "       a     = 2;\n"
                        "       bbb >>= 2;\n"
                        "       ```",
                )]

            ConsecutiveAssignments: Annotated[_ConsecutiveAssignments, Field(
                default_factory=_ConsecutiveAssignments.model_construct,
                description="Options for aligning consecutive assignments",
            )]

            class _ConsecutiveBitFields(BaseModel):
                """Validate the `[tool.clang-format.align.ConsecutiveBitFields]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align consecutive bit field declarations:\n"
                        "    `true`:\n"
                        "       ```cpp\n"
                        "       int aaaa : 1;\n"
                        "       int b    : 12;\n"
                        "       int ccc  : 8;\n"
                        "       ```\n"
                        "    `false`: preserve user formatting.",
                )]
                AcrossEmptyLines: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align across empty lines:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       int aaaa : 1;\n"
                        "       int b    : 12;\n"
                        "       int ccc  : 8;\n"
                        "\n"
                        "       int d    : 3;\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       int aaaa : 1;\n"
                        "       int b    : 12;\n"
                        "       int ccc  : 8;\n"
                        "\n"
                        "       int d : 3;\n"
                        "       ```",
                )]
                AcrossComments: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align across comments:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       int d    : 3;\n"
                        "       /* A comment. */\n"
                        "       int eeee : 4;\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       int d : 3;\n"
                        "       /* A comment. */\n"
                        "       int eeee : 4;\n"
                        "       ```",
                )]

            ConsecutiveBitFields: Annotated[_ConsecutiveBitFields, Field(
                default_factory=_ConsecutiveBitFields.model_construct,
                description="Options for aligning consecutive bit field declarations",
            )]

            class _ConsecutiveDeclarations(BaseModel):
                """Validate the `[tool.clang-format.align.ConsecutiveDeclarations]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align consecutive declarations:\n"
                        "    `true`:\n"
                        "       ```cpp\n"
                        "       int         aaaa = 12;\n"
                        "       float       b = 23;\n"
                        "       std::string ccc;\n"
                        "       ```\n"
                        "    `false`: preserve user formatting.",
                )]
                AcrossEmptyLines: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align across empty lines:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       int         aaaa = 12;\n"
                        "       float       b = 23;\n"
                        "       std::string ccc;\n"
                        "\n"
                        "       double      d = 3.14;\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       int         aaaa = 12;\n"
                        "       float       b = 23;\n"
                        "       std::string ccc;\n"
                        "\n"
                        "       double d = 3.14;\n"
                        "       ```",
                )]
                AcrossComments: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align across comments:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       int    d = 3;\n"
                        "       /* A comment. */\n"
                        "       double eee;\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       int d = 3;\n"
                        "       /* A comment. */\n"
                        "       double eee;\n"
                        "       ```",
                )]
                AlignFunctionDeclarations: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to horizontally align function declarations "
                        "similar to variable declarations:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       unsigned int f1(void);\n"
                        "       void         f2(void);\n"
                        "       size_t       f3(void);\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       unsigned int f1(void);\n"
                        "       void f2(void);\n"
                        "       size_t f3(void);\n"
                        "       ```",
                )]
                AlignFunctionPointers: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to horizontally align function pointer "
                        "declarations similar to variable declarations:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       unsigned i;\n"
                        "       int     &r;\n"
                        "       int     *p;\n"
                        "       int      (*f)();\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       unsigned i;\n"
                        "       int     &r;\n"
                        "       int     *p;\n"
                        "       int (*f)();\n"
                        "       ```",
                )]

            ConsecutiveDeclarations: Annotated[_ConsecutiveDeclarations, Field(
                default_factory=_ConsecutiveDeclarations.model_construct,
                description=
                    "Options for aligning consecutive function/variable declarations",
            )]

            class _ConsecutiveMacros(BaseModel):
                """Validate the `[tool.clang-format.align.ConsecutiveMacros]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align consecutive macro definitions:\n"
                        "    `true`:\n"
                        "       ```cpp\n"
                        "       #define SHORT_NAME       42\n"
                        "       #define LONGER_NAME      0x007\n"
                        "       #define EVEN_LONGER_NAME (2)\n"
                        "       #define foo(x)           (x * x)\n"
                        "       #define bar(y, z)        (y + z)\n"
                        "       ```\n"
                        "    `false`: preserve user formatting.",
                )]
                AcrossEmptyLines: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align across empty lines:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       #define SHORT_NAME       42\n"
                        "       #define LONGER_NAME      0x007\n"
                        "       #define EVEN_LONGER_NAME (2)\n"
                        "\n"
                        "       #define foo(x)           (x * x)\n"
                        "       #define bar(y, z)        (y + z)\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       #define SHORT_NAME       42\n"
                        "       #define LONGER_NAME      0x007\n"
                        "       #define EVEN_LONGER_NAME (2)\n"
                        "\n"
                        "       #define foo(x) (x * x)\n"
                        "       #define bar(y, z) (y + z)\n"
                        "       ```",
                )]
                AcrossComments: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align across comments:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       #define foo(x)           (x * x)\n"
                        "       /* A comment. */\n"
                        "       #define bar(y, z)        (y + z)\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       #define foo(x) (x * x)\n"
                        "       /* A comment. */\n"
                        "       #define bar(y, z) (y + z)\n"
                        "       ```",
                )]

            ConsecutiveMacros: Annotated[_ConsecutiveMacros, Field(
                default_factory=_ConsecutiveMacros.model_construct,
                description="Options for aligning consecutive macro definitions",
            )]

            class _ConsecutiveShortCaseStatements(BaseModel):
                """Validate the `[tool.clang-format.align.ConsecutiveShortCaseStatements]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Enabled: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align consecutive short case labels:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       switch (level) {\n"
                        "       case log::info:    return \"info:\";\n"
                        "       case log::warning: return \"warning:\";\n"
                        "       default:           return \"\";\n"
                        "       }\n"
                        "       ```\n"
                        "   `false`: preserve user formatting.",
                )]
                AcrossEmptyLines: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align across empty lines:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       switch (level) {\n"
                        "       case log::info:    return \"info:\";\n"
                        "       case log::warning: return \"warning:\";\n"
                        "\n"
                        "       default:           return \"\";\n"
                        "       }\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       switch (level) {\n"
                        "       case log::info:    return \"info:\";\n"
                        "       case log::warning: return \"warning:\";\n"
                        "\n"
                        "       default: return \"\";\n"
                        "       }\n"
                        "       ```",
                )]
                AcrossComments: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to align across comments:\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       switch (level) {\n"
                        "       case log::info:    return \"info:\";\n"
                        "       /* A comment. */\n"
                        "       case log::warning: return \"warning:\";\n"
                        "       default:           return \"\";\n"
                        "       }\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       switch (level) {\n"
                        "       case log::info:    return \"info:\";\n"
                        "       /* A comment. */\n"
                        "       case log::warning: return \"warning:\";\n"
                        "       default: return \"\";\n"
                        "       }\n"
                        "       ```",
                )]
                AlignCaseArrows: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to also align case arrows when aligning "
                        "short case expressions.\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       i = switch (day) {\n"
                        "           case THURSDAY, SATURDAY -> 8;\n"
                        "           case WEDNESDAY          -> 9;\n"
                        "           default                 -> 0;\n"
                        "       };\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       i = switch (day) {\n"
                        "           case THURSDAY, SATURDAY -> 8;\n"
                        "           case WEDNESDAY ->          9;\n"
                        "           default ->                 0;\n"
                        "       };\n"
                        "       ```",
                )]
                AlignCaseColons: Annotated[bool, Field(
                    default=False,
                    description=
                        "Controls whether to also align case colons when aligning "
                        "short case labels.\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       switch (level) {\n"
                        "       case log::info   : return \"info:\";\n"
                        "       case log::warning: return \"warning:\";\n"
                        "       default          : return \"\";\n"
                        "       }\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       switch (level) {\n"
                        "       case log::info:    return \"info:\";\n"
                        "       case log::warning: return \"warning:\";\n"
                        "       default:           return \"\";\n"
                        "       }\n"
                        "       ```",
                )]

            ConsecutiveShortCaseStatements: Annotated[_ConsecutiveShortCaseStatements, Field(
                default_factory=_ConsecutiveShortCaseStatements.model_construct,
                description="Options for aligning consecutive short case labels",
            )]

            class _TrailingComments(BaseModel):
                """Validate the `[tool.clang-format.align.TrailingComments]` table."""
                model_config = ConfigDict(extra="forbid")
                Kind: Annotated[Literal["Never", "Leave", "Always"], Field(
                    default="Leave",
                    examples=["Never", "Leave", "Always"],
                    description=
                        "Controls the alignment of trailing comments before a line"
                        "break or scope change:\n"
                        "   `Never`: don't align trailing comments but other "
                        "formatting still applies.\n"
                        "       ```cpp\n"
                        "       int a; // comment\n"
                        "       int ab; // comment\n"
                        "\n"
                        "       int abc; // comment\n"
                        "       int abcd; // comment\n"
                        "       ```\n"
                        "   `Leave`: preserve user formatting.\n"
                        "       ```cpp\n"
                        "       int a;      // comment\n"
                        "       int ab;         // comment\n"
                        "\n"
                        "       int abc;    // comment\n"
                        "       int abcd;       // comment\n"
                        "       ```\n"
                        "   `Always`: align trailing comments.\n"
                        "       ```cpp\n"
                        "       int a;  // comment\n"
                        "       int ab; // comment\n"
                        "\n"
                        "       int abc;  // comment\n"
                        "       int abcd; // comment\n"
                        "       ```\n"
                )]
                OverEmptyLines: Annotated[NonNegativeInt, Field(
                    default=1,
                    description=
                        "Controls how many empty lines are needed to reset alignment.  "
                        "When set to 2, it formats like below:\n"
                        "   ```cpp\n"
                        "   int a;      // all these\n"
                        "\n"
                        "   int ab;     // comments are\n"
                        "\n"
                        "\n"
                        "   int abcdef; // aligned\n"
                        "   ```\n"
                        "If set to 1, it formats like:\n"
                        "   ```cpp\n"
                        "   int a;  // these are\n"
                        "\n"
                        "   int ab; // aligned\n"
                        "\n"
                        "\n"
                        "   int abcdef; // but this isn't\n"
                        "   ```\n"
                )]
                AlignPPAndNotPP: Annotated[bool, Field(
                    default=True,
                    description=
                        "Controls whether comments following a preprocessor directive "
                        "should be aligned with comments that don't.\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       #define A  // Comment\n"
                        "       #define AB // Aligned\n"
                        "       int i;     // Aligned\n"
                        "       ```\n"
                        "   `false`:\n"
                        "       ```cpp\n"
                        "       #define A  // Comment\n"
                        "       #define AB // Aligned\n"
                        "       int i; // Not aligned\n"
                        "       ```",
                )]

            TrailingComments: Annotated[_TrailingComments, Field(
                default_factory=_TrailingComments.model_construct,
                description="Options for aligning trailing `//` comments",
            )]

        Align: Annotated[_Align, Field(
            default_factory=_Align.model_construct,
            description="Options for horizontally aligning code",
        )]

        class _Allow(BaseModel):
            """Validate the `[tool.clang-format.Allow]` table."""
            model_config = ConfigDict(extra="forbid")
            AllArgumentsOnNextLine: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to allow putting all arguments of a function "
                    "call on the next line if they don't fit on the current line, "
                    "even if `BinPackArguments` is false.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       callFunction(\n"
                    "           a, b, c, d);\n"
                    "       ```\n"
                    "   `false`: preserve other formatting.",
            )]
            AllParametersOfDeclarationOnNextLine: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to allow putting all parameters of a function "
                    "declaration on the next line if they don't fit on the current "
                    "line, even if `BinPackParameters` is set to `OnePerLine`.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       void myFunction(\n"
                    "           int a, int b, int c, int d, int e);\n"
                    "       ```\n"
                    "   `false`: preserve other formatting.",
            )]
            BreakBeforeNoexceptSpecifier: Annotated[
                Literal["Never", "OnlyWithParen", "Always"],
                Field(
                    default="OnlyWithParen",
                    examples=["Never", "OnlyWithParen", "Always"],
                    description=
                        "Controls whether there could be a line break before a "
                        "`noexcept` specifier.\n"
                        "   `Never`: no line break allowed.\n"
                        "       ```cpp\n"
                        "       void foo(int arg1,\n"
                        "                double arg2) noexcept;\n"
                        "\n"
                        "       void bar(int arg1, double arg2) noexcept(\n"
                        "           noexcept(baz(arg1)) &&\n"
                        "           noexcept(baz(arg2)));\n"
                        "       ```\n"
                        "   `OnlyWithParen`: line breaks are only allowed for "
                        "conditional `noexcept` specifiers.\n"
                        "       ```cpp\n"
                        "       void foo(int arg1,\n"
                        "                double arg2) noexcept;\n"
                        "\n"
                        "       void bar(int arg1, double arg2)\n"
                        "           noexcept(noexcept(baz(arg1)) &&\n"
                        "                    noexcept(baz(arg2)));\n"
                        "       ```\n"
                        "   `Always`: line breaks are allowed, but may be suppressed "
                        "by other formatting rules.\n"
                        "       ```cpp\n"
                        "       void foo(int arg1,\n"
                        "                double arg2) noexcept;\n"
                        "\n"
                        "       void bar(int arg1, double arg2)\n"
                        "           noexcept(noexcept(baz(arg1)) &&\n"
                        "                    noexcept(baz(arg2)));\n"
                        "       ```",
                )
            ]
            ShortBlocksOnASingleLine: Annotated[Literal["Never", "Empty", "Always"], Field(
                default="Empty",
                examples=["Never", "Empty", "Always"],
                description=
                    "Controls whether to attempt to place short control blocks on a "
                    "single line.\n"
                    "   `Never`: never merge blocks into a single line.\n"
                    "       ```cpp\n"
                    "       while (true) {\n"
                    "       }\n"
                    "       while (true) {\n"
                    "           continue;\n"
                    "       }\n"
                    "       ```\n"
                    "   `Empty`: only merge empty blocks into a single line.\n"
                    "       ```cpp\n"
                    "       while (true) {}\n"
                    "       while (true) {\n"
                    "           continue;\n"
                    "       }\n"
                    "       ```\n"
                    "   `Always`: always merge short blocks into a single line.\n"
                    "       ```cpp\n"
                    "       while (true) {}\n"
                    "       while (true) { continue; }\n"
                    "       ```",
            )]
            ShortCaseExpressionsOnASingleLine: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to merge a short switch labeled rule into a "
                    "single line.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       switch (a) {\n"
                    "       case 1 -> 1;\n"
                    "       default -> 0;\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       switch (a) {\n"
                    "       case 1 ->\n"
                    "           1;\n"
                    "       default ->\n"
                    "           0;\n"
                    "       }\n"
                    "       ```",
            )]
            ShortCaseLabelsOnASingleLine: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to contract short case labels to a single line.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       switch (a) {\n"
                    "       case 1: x = 1; break;\n"
                    "       case 2: return;\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       switch (a) {\n"
                    "       case 1:\n"
                    "           x = 1;\n"
                    "           break;\n"
                    "       case 2:\n"
                    "           return;\n"
                    "       }\n"
                    "       ```",
            )]
            ShortCompoundRequirementsOnASingleLine: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to allow short compound requirements on a "
                    "single line.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       template <typename T>\n"
                    "       concept c = requires(T x) {\n"
                    "           { x + 1 } -> std::same_as<int>;\n"
                    "       };\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       template <typename T>\n"
                    "       concept c = requires(T x) {\n"
                    "           {\n"
                    "               x + 1\n"
                    "           } -> std::same_as<int>;\n"
                    "       };\n"
                    "       ```",
            )]
            ShortEnumsOnASingleLine: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to allow short enums on a single line.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       enum { A, B } myEnum;\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       enum {\n"
                    "           A,\n"
                    "           B\n"
                    "       } myEnum;\n"
                    "       ```",
            )]

            class _ShortFunctionsOnASingleLine(BaseModel):
                """Validate the `[tool.clang-format.Allow.ShortFunctionsOnASingleLine]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Empty: Annotated[bool, Field(
                    default=True,
                    description=
                        "Controls whether to allow empty functions on a single line.\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       void f() {}\n"
                        "       void f2() {\n"
                        "           bar2();\n"
                        "       }\n"
                        "       void f3() { /* comment */ }\n"
                        "       ```\n"
                        "   `false`: preserve user formatting.",
                )]
                Inline: Annotated[bool, Field(
                    default=True,
                    description=
                        "Controls whether to merge functions defined inside a class.\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       class Foo {\n"
                        "           void f() { foo(); }\n"
                        "           void g() {}\n"
                        "       };\n"
                        "       void f() {\n"
                        "           foo();\n"
                        "       }\n"
                        "       void f() {\n"
                        "       }\n"
                        "       ```\n"
                        "   `false`: preserve user formatting.",
                )]
                Other: Annotated[bool, Field(
                    default=True,
                    description=
                        "Controls whether to merge all functions fitting on a single "
                        "line.  Note that this control does not include 'Empty'.\n"
                        "   `true`:\n"
                        "       ```cpp\n"
                        "       class Foo {\n"
                        "           void f() { foo(); }\n"
                        "       };\n"
                        "       void f() { bar(); }\n"
                        "       ```\n"
                        "   `false`: preserve user formatting.",
                )]

            ShortFunctionsOnASingleLine: Annotated[_ShortFunctionsOnASingleLine, Field(
                default_factory=_ShortFunctionsOnASingleLine.model_construct,
                description="Options for allowing short functions on a single line",
            )]
            ShortIfStatementsOnASingleLine: Annotated[
                Literal["None", "WithoutElse", "OnlyFirstIf", "AllIfsAndElse"],
                Field(
                    default="WithoutElse",
                    examples=["None", "WithoutElse", "OnlyFirstIf", "AllIfsAndElse"],
                    description=
                        "Controls whether to attempt to place short if statements on a "
                        "single line.\n"
                        "   `None`: never put if statements into a single line.\n"
                        "       ```cpp\n"
                        "       if (a)\n"
                        "           return;\n"
                        "\n"
                        "       if (b)\n"
                        "           return;\n"
                        "       else\n"
                        "           return;\n"
                        "\n"
                        "       if (c)\n"
                        "           return;"
                        "       else {\n"
                        "           return;\n"
                        "       }\n"
                        "       ```\n"
                        "   `WithoutElse`: put short ifs on the same line only if they "
                        "lack an `else` statement.\n"
                        "       ```cpp\n"
                        "       if (a) return;\n"
                        "\n"
                        "       if (b)\n"
                        "           return;\n"
                        "       else\n"
                        "           return;\n"
                        "\n"
                        "       if (c)\n"
                        "           return;"
                        "       else {\n"
                        "           return;\n"
                        "       }\n"
                        "       ```\n"
                        "   `OnlyFirstIf`: put short ifs, but not else ifs nor else "
                        "statements, on the same line.\n"
                        "       ```cpp\n"
                        "       if (a) return;\n"
                        "\n"
                        "       if (b) return;\n"
                        "       else if (b)\n"
                        "           return;\n"
                        "       else\n"
                        "           return;\n"
                        "\n"
                        "       if (c) return;"
                        "       else {\n"
                        "           return;\n"
                        "       }\n"
                        "       ```\n"
                        "   `AllIfsAndElse`: always put short ifs, else ifs, and else "
                        "statements on the same line.\n"
                        "       ```cpp\n"
                        "       if (a) return;\n"
                        "\n"
                        "       if (b) return;\n"
                        "       else return;\n"
                        "\n"
                        "       if (c) return;"
                        "       else {\n"
                        "           return;\n"
                        "       }\n"
                        "       ```",
                )
            ]
            ShortLambdasOnASingleLine: Annotated[
                Literal["None", "Empty", "Inline", "All"],
                Field(
                    default="All",
                    examples=["None", "Empty", "Inline", "All"],
                    description=
                        "Controls whether to attempt to place short lambda expressions "
                        "on a single line.\n"
                        "   `None`: never merge lambda expressions into a single line.\n"
                        "   `Empty`: only merge empty lambdas.\n"
                        "       ```cpp\n"
                        "       auto lambda = [](int a) {};\n"
                        "       auto lambda2 = [](int a) {\n"
                        "           return a;\n"
                        "       };\n"
                        "       ```\n"
                        "   `Inline`: only merge lambdas into a single line if they are "
                        "an argument of a function.\n"
                        "       ```cpp\n"
                        "       auto lambda = [](int x, int y) {\n"
                        "           return x < y;\n"
                        "       };\n"
                        "       sort(a.begin(), a.end(), [](int x, int y) { return x < y; });\n"
                        "       ```\n"
                        "   `All`: merge all lambdas that fit on a single line.\n"
                        "       ```cpp\n"
                        "       auto lambda = [](int a) {};\n"
                        "       auto lambda2 = [](int a) { return a; };\n"
                        "       ```",
                )
            ]
            ShortLoopsOnASingleLine: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to attempt to place short loops on a single line.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       while (true) {}\n"
                    "       for (int i = 0; i < 10; ++i) {}\n"
                    "       do {} while (true);\n"
                    "       ```\n"
                    "   `false`: preserve user formatting.",
            )]
            ShortNamespacesOnASingleLine: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to attempt to place short namespaces on a single line.\n"
                    "   `true`: merge short namespaces into a single line.\n"
                    "       ```cpp\n"
                    "       namespace a { class b; }\n"
                    "       ```\n"
                    "   `false`: preserve user formatting.",
            )]

        Allow: Annotated[_Allow, Field(
            default_factory=_Allow.model_construct,
            description="Options for compacting short constructs.",
        )]

        class _Break(BaseModel):
            """Validate the `[tool.clang-format.Break]` table."""
            model_config = ConfigDict(extra="forbid")
            AdjacentStringLiterals: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to break adjacent string literals.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       return \"Code\"\n"
                    "              \"\0\52\26\55\55\0\"\n"
                    "              \"x013\"\n"
                    "              \"\02\xBA\"\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       return \"Code\" \"\0\52\26\55\55\0\" \"x013\" \"\02\xBA\";\n"
                    "       ```",
            )]
            AfterAttributes: Annotated[Literal["Never", "Leave", "Always"], Field(
                default="Never",
                examples=["Never", "Leave", "Always"],
                description=
                    "Controls line breaks after groups of C++11 attributes before "
                    "declarations or control statements.\n"
                    "   `Never`: never break after the last attribute of the group.\n"
                    "       ```cpp\n"
                    "       [[maybe_unused]] const int i;\n"
                    "       [[gnu::const]] [[maybe_unused]] int j;\n"
                    "\n"
                    "       [[nodiscard]] inline int f();\n"
                    "       [[gnu::const]] [[nodiscard]] int g();\n"
                    "\n"
                    "       [[likely]] if (a)\n"
                    "           f();\n"
                    "       else\n"
                    "           g();\n"
                    "\n"
                    "       switch (b) {\n"
                    "       [[unlikely]] case 1:\n"
                    "           ++b;\n"
                    "           break;\n"
                    "       [[likely]] default:\n"
                    "           return;\n"
                    "       }\n"
                    "       ```\n"
                    "   `Leave`: preserve user formatting.\n"
                    "       ```cpp\n"
                    "       [[maybe_unused]] const int i;\n"
                    "       [[gnu::const]] [[maybe_unused]]\n"
                    "       int j;\n"
                    "\n"
                    "       [[nodiscard]] inline int f();\n"
                    "       [[gnu::const]] [[nodiscard]]\n"
                    "       int g();\n"
                    "\n"
                    "       [[likely]] if (a)\n"
                    "           f();\n"
                    "       else\n"
                    "           g();\n"
                    "\n"
                    "       switch (b) {\n"
                    "       [[unlikely]] case 1:\n"
                    "           ++b;\n"
                    "           break;\n"
                    "       [[likely]]\n"
                    "       default:\n"
                    "           return;\n"
                    "       }\n"
                    "       ```\n"
                    "   `LeaveAll`: same as `Leave` except that it applies to all "
                    "attributes of the group.\n"
                    "       ```cpp\n"
                    "       [[deprecated(\"Don't use this version\")]]\n"
                    "       [[nodiscard]]\n"
                    "       bool foo() {\n"
                    "           return true;\n"
                    "       }\n"
                    "\n"
                    "       [[deprecated(\"Don't use this version\")]]\n"
                    "       [[nodiscard]] bool bar() {\n"
                    "           return true;\n"
                    "       }\n"
                    "       ```\n"
                    "   `Always`: always break after the last attribute of the group.\n"
                    "       ```cpp\n"
                    "       [[maybe_unused]]\n"
                    "       const int i;\n"
                    "       [[gnu::const]] [[maybe_unused]]\n"
                    "       int j;\n"
                    "\n"
                    "       [[nodiscard]]\n"
                    "       inline int f();\n"
                    "       [[gnu::const]] [[nodiscard]]\n"
                    "       int g();\n"
                    "\n"
                    "       [[likely]]\n"
                    "       if (a)\n"
                    "           f();\n"
                    "       else\n"
                    "           g();\n"
                    "\n"
                    "       switch (b) {\n"
                    "       [[unlikely]]\n"
                    "       case 1:\n"
                    "           ++b;\n"
                    "           break;\n"
                    "       [[likely]]\n"
                    "       default:\n"
                    "           return;\n"
                    "       }\n"
                    "       ```",
            )]
            AfterOpenBracketBracedList: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to force a break after the left bracket of a "
                    "braced initializer list when the list exceeds `ColumnLimit`.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       vector<int> x {\n"
                    "           1, 2, 3}\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       vector<int> x {1,\n"
                    "           2, 3}\n"
                    "       ```",
            )]
            AfterOpenBracketFunction: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to force a break after the left parenthesis of a "
                    "function (declaration, definition, call) when the parameters "
                    "exceed `ColumnLimit`.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       foo (\n"
                    "           a, b)\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       foo(a,\n"
                    "           b)\n"
                    "       ```",
            )]
            AfterOpenBracketIf: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to force a break after the left parenthesis of "
                    "an `if` control statement once the expression exceeds `ColumnLimit`.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       if constexpr (\n"
                    "           a || b)\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       if constexpr (a ||\n"
                    "                     b)\n"
                    "       ```",
            )]
            AfterOpenBracketLoop: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to force a break after the left parenthesis in "
                    "a loop control statement once the condition exceeds `ColumnLimit`.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       while (\n"
                    "           a && b) {\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       while (a &&\n"
                    "              b) {\n"
                    "       }\n"
                    "       ```",
            )]
            AfterOpenBracketSwitch: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to force a break after the left parenthesis in "
                    "`switch` control statements once the condition exceeds `ColumnLimit`.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       switch (\n"
                    "           a + b) {\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       switch (a +\n"
                    "               b) {\n"
                    "       }\n"
                    "       ```",
            )]
            AfterReturnType: Annotated[
                Literal[
                    "Automatic", "ExceptShortType", "TopLevel",
                    "TopLevelDefinitions", "All", "AllDefinitions",
                ],
                Field(
                    default="ExceptShortType",
                    examples=[
                        "Automatic", "ExceptShortType", "TopLevel",
                        "TopLevelDefinitions", "All", "AllDefinitions",
                    ],
                    description=
                        "Controls line breaks around return types in function "
                        "declarations and definitions.\n"
                        "   `Automatic`: break after the return type based on "
                        "configured penalties.\n"
                        "       ```cpp\n"
                        "       class A {\n"
                        "           int f() { return 0; };\n"
                        "       };\n"
                        "       int f();\n"
                        "       int f() { return 1; }\n"
                        "       int\n"
                        "       LongName::AnotherLongName();\n"
                        "       ```\n"
                        "   `ExceptShortType`: same as `Automatic`, except that there "
                        "is no break after short return types.\n"
                        "       ```cpp\n"
                        "       class A {\n"
                        "           int f() { return 0; };\n"
                        "       };\n"
                        "       int f();\n"
                        "       int f() { return 1; }\n"
                        "       int LongName::\n"
                        "           AnotherLongName();\n"
                        "       ```\n"
                        "   `TopLevel`: always break after the return types of "
                        "top-level functions.\n"
                        "       ```cpp\n"
                        "       class A {\n"
                        "           int f() { return 0; };\n"
                        "       };\n"
                        "       int\n"
                        "       f();\n"
                        "       int f() {\n"
                        "           return 1;\n"
                        "       }\n"
                        "       int\n"
                        "       LongName::AnotherLongName();\n"
                        "       ```\n"
                        "   `TopLevelDefinitions`: always break after the return type "
                        "of top-level definitions.\n"
                        "       ```cpp\n"
                        "       class A {\n"
                        "           int f() { return 0; };\n"
                        "       };\n"
                        "       int f();\n"
                        "       int\n"
                        "       f() {\n"
                        "           return 1;\n"
                        "       }\n"
                        "       int\n"
                        "       LongName::AnotherLongName();\n"
                        "       ```\n"
                        "   `All`: always break after the return type.\n"
                        "       ```cpp\n"
                        "       class A {\n"
                        "           int\n"
                        "           f() {\n"
                        "               return 0;\n"
                        "           };\n"
                        "       };\n"
                        "       int\n"
                        "       f();\n"
                        "       int\n"
                        "       f() {\n"
                        "           return 1;\n"
                        "       }\n"
                        "       int\n"
                        "       LongName::AnotherLongName();\n"
                        "       ```\n"
                        "   `AllDefinitions`: always break after the return type of "
                        "function definitions.\n"
                        "    ```cpp\n"
                        "       class A {\n"
                        "           int\n"
                        "           f() {\n"
                        "               return 0;\n"
                        "           };\n"
                        "       };\n"
                        "       int f();\n"
                        "       int\n"
                        "       f() {\n"
                        "           return 1;\n"
                        "       }\n"
                        "       int\n"
                        "       LongName::AnotherLongName();\n"
                        "    ```",
                )
            ]
            BeforeBinaryOperators: Annotated[Literal["None", "NonAssignment", "All"], Field(
                default="None",
                examples=["None", "NonAssignment", "All"],
                description=
                    "Controls where wrapped binary operators are placed.\n"
                    "   `None`: break after operators.\n"
                    "       ```cpp\n"
                    "       LooooooooooongType loooooooooooooooooooooongVariable =\n"
                    "           someLooooooooooooooooongFunction();\n"
                    "\n"
                    "       bool value = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa +\n"
                    "                            aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa ==\n"
                    "                        aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa &&\n"
                    "                    aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa >\n"
                    "                        ccccccccccccccccccccccccccccccccccccccccc;\n"
                    "       ```\n"
                    "   `NonAssignment`: break before non-assignment operators.\n"
                    "       ```cpp\n"
                    "       LooooooooooongType loooooooooooooooooooooongVariable =\n"
                    "           someLooooooooooooooooongFunction();\n"
                    "\n"
                    "       bool value = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                    "                            + aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                    "                        == aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                    "                    && aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                    "                           > ccccccccccccccccccccccccccccccccccccccccc;\n"
                    "       ```\n"
                    "   `All`: break before all operators, including assignment.\n"
                    "       ```cpp\n"
                    "       LooooooooooongType loooooooooooooooooooooongVariable\n"
                    "           = someLooooooooooooooooongFunction();\n"
                    "\n"
                    "       bool value = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                    "                            + aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                    "                        == aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                    "                    && aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                    "                           > ccccccccccccccccccccccccccccccccccccccccc;\n"
                    "       ```",
            )]
            BeforeCloseBracketBracedList: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to force a break before the right bracket of a "
                    "braced initializer list when the list exceeds `ColumnLimit`.  This "
                    "only applies when there is a break after the opening bracket.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       vector<int> x {\n"
                    "           1, 2, 3\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       vector<int> x {\n"
                    "           1, 2, 3}\n"
                    "       ```",
            )]
            BeforeCloseBracketFunction: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to force a break before the right parenthesis of "
                    "a function (declaration, definition, call) when the parameters "
                    "exceed `ColumnLimit`.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       foo(\n"
                    "           a, b\n"
                    "       )\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       foo(\n"
                    "           a, b)\n"
                    "       ```",
            )]
            BeforeCloseBracketIf: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to force a break before the right parenthesis of "
                    "an `if` control statement when the condition exceeds `ColumnLimit`.  "
                    "This only applies when there is a break after the opening parenthesis.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       if constexpr (\n"
                    "           a || b\n"
                    "       )\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       if constexpr (\n"
                    "           a || b)\n"
                    "       ```",
            )]
            BeforeCloseBracketLoop: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to force a break before the right parenthesis of "
                    "a loop control statement when the condition exceeds `ColumnLimit`.  "
                    "This only applies when there is a break after the opening parenthesis.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       while (\n"
                    "           a && b\n"
                    "       )\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       while (\n"
                    "           a && b)\n"
                    "       ```",
            )]
            BeforeCloseBracketSwitch: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to force a break before the right parenthesis of "
                    "a `switch` control statement when the condition exceeds `ColumnLimit`.  "
                    "This only applies when there is a break after the opening parenthesis.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       switch (\n"
                    "           a + b\n"
                    "       )\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       switch (\n"
                    "           a + b)\n"
                    "       ```",
            )]
            BeforeConceptDeclarations: Annotated[Literal["Never", "Allowed", "Always"], Field(
                default="Always",
                examples=["Never", "Allowed", "Always"],
                description=
                    "Controls line breaking before concept declarations.\n"
                    "   `Never`: keep the template declaration and `concept` on one line.\n"
                    "       ```cpp\n"
                    "       template <typename T> concept C = ...;\n"
                    "       ```\n"
                    "   `Allowed`: allow breaking before `concept` according to other "
                    "line-breaking rules and penalties.\n"
                    "   `Always`: always break before `concept`.\n"
                    "       ```cpp\n"
                    "       template <typename T>\n"
                    "       concept C = ...;\n"
                    "       ```",
            )]
            BeforeInlineASMColon: Annotated[Literal["Never", "OnlyMultiline", "Always"], Field(
                default="OnlyMultiline",
                examples=["Never", "OnlyMultiline", "Always"],
                description=
                    "Controls line breaking before inline ASM colons.\n"
                    "   `Never`: never break before the inline ASM colon.\n"
                    "       ```cpp\n"
                    "       asm volatile(\"string\", : : val);\n"
                    "       ```\n"
                    "   `OnlyMultiline`: break before the colon only when the statement "
                    "exceeds `ColumnLimit`.\n"
                    "       ```cpp\n"
                    "       asm volatile(\"string\", : : val);\n"
                    "       asm(\"cmoveq %1, %2, %[result]\"\n"
                    "           : [result] \"=r\"(result)\n"
                    "           : \"r\"(test), \"r\"(new), \"[result]\"(old));\n"
                    "       ```\n"
                    "   `Always`: always break before inline ASM colons.\n"
                    "       ```cpp\n"
                    "       asm volatile(\"string\",\n"
                    "                    :\n"
                    "                    : val);\n"
                    "       ```",
            )]
            BeforeTemplateCloser: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to break before a template closing bracket (`>`) "
                    "when there is a line break after the matching opening bracket (`<`).\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       template <typename Foo, typename Bar>\n"
                    "\n"
                    "       template <typename Foo,\n"
                    "                 typename Bar>\n"
                    "\n"
                    "       template <\n"
                    "           typename Foo,\n"
                    "           typename Bar\n"
                    "       >\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       template <typename Foo, typename Bar>\n"
                    "\n"
                    "       template <typename Foo,\n"
                    "                 typename Bar>\n"
                    "\n"
                    "       template <\n"
                    "           typename Foo,\n"
                    "           typename Bar>\n"
                    "       ```",
            )]
            BeforeTernaryOperators: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether ternary operators are placed after a line break.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongDescription\n"
                    "           ? firstValue\n"
                    "           : SecondValueVeryVeryVeryVeryLong;\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongDescription ?\n"
                    "           firstValue :\n"
                    "           SecondValueVeryVeryVeryVeryLong;\n"
                    "       ```",
            )]
            BinaryOperations: Annotated[
                Literal["Never", "OnePerLine", "RespectPrecedence"],
                Field(
                    default="Never",
                    examples=["Never", "OnePerLine", "RespectPrecedence"],
                    description=
                        "Controls line breaking for chains of binary operations.\n"
                        "   `Never`: do not force per-operation breaking.\n"
                        "       ```cpp\n"
                        "       aaa + bbbb * ccccc - ddddd +\n"
                        "       eeeeeeeeeeeeeeee;\n"
                        "       ```\n"
                        "   `OnePerLine`: if the chain does not fit on one line, put "
                        "each operation on its own line.\n"
                        "       ```cpp\n"
                        "       aaa +\n"
                        "       bbbb *\n"
                        "       ccccc -\n"
                        "       ddddd +\n"
                        "       eeeeeeeeeeeeeeee;\n"
                        "       ```\n"
                        "   `RespectPrecedence`: break long chains by precedence groups.\n"
                        "       ```cpp\n"
                        "       aaa +\n"
                        "       bbbb * ccccc -\n"
                        "       ddddd +\n"
                        "       eeeeeeeeeeeeeeee;\n"
                        "       ```",
                )
            ]
            ConstructorInitializers: Annotated[
                Literal["BeforeColon", "BeforeComma", "AfterColon", "AfterComma"],
                Field(
                    default="AfterColon",
                    examples=["BeforeColon", "BeforeComma", "AfterColon", "AfterComma"],
                    description=
                        "Controls line breaking for constructor initializer lists.\n"
                        "   `BeforeColon`: break before `:` and after commas.\n"
                        "       ```cpp\n"
                        "       Constructor()\n"
                        "           : initializer1(),\n"
                        "             initializer2()\n"
                        "       ```\n"
                        "   `BeforeComma`: break before `:` and before commas, aligning "
                        "commas with the colon.\n"
                        "       ```cpp\n"
                        "       Constructor()\n"
                        "           : initializer1()\n"
                        "           , initializer2()\n"
                        "       ```\n"
                        "   `AfterColon`: break after `:` and after commas.\n"
                        "       ```cpp\n"
                        "       Constructor() :\n"
                        "           initializer1(),\n"
                        "           initializer2()\n"
                        "       ```\n"
                        "   `AfterComma`: break only after commas.\n"
                        "       ```cpp\n"
                        "       Constructor() : initializer1(),\n"
                        "                       initializer2()\n"
                        "       ```",
                )
            ]
            FunctionDefinitionParameters: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to always break before function definition "
                    "parameters.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       void functionDefinition(\n"
                    "                int A, int B) {}\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       void functionDefinition(int A, int B) {}\n"
                    "       ```",
            )]
            InheritanceList: Annotated[
                Literal["BeforeColon", "BeforeComma", "AfterColon", "AfterComma"],
                Field(
                    default="AfterColon",
                    examples=["BeforeColon", "BeforeComma", "AfterColon", "AfterComma"],
                    description=
                        "Controls line breaking for class inheritance lists.\n"
                        "   `BeforeColon`: break before `:` and after commas.\n"
                        "       ```cpp\n"
                        "       class Foo\n"
                        "           : Base1,\n"
                        "             Base2\n"
                        "       {};\n"
                        "       ```\n"
                        "   `BeforeComma`: break before `:` and before commas, aligning "
                        "commas with the colon.\n"
                        "       ```cpp\n"
                        "       class Foo\n"
                        "           : Base1\n"
                        "           , Base2\n"
                        "       {};\n"
                        "       ```\n"
                        "   `AfterColon`: break after `:` and after commas.\n"
                        "       ```cpp\n"
                        "       class Foo :\n"
                        "           Base1,\n"
                        "           Base2\n"
                        "       {};\n"
                        "       ```\n"
                        "   `AfterComma`: break only after commas.\n"
                        "       ```cpp\n"
                        "       class Foo : Base1,\n"
                        "                   Base2\n"
                        "       {};\n"
                        "       ```",
                )
            ]
            StringLiterals: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether clang-format may break long string literals.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       const char* x = \"veryVeryVeryVeryVeryVe\"\n"
                    "                       \"ryVeryVeryVeryVeryVery\"\n"
                    "                       \"VeryLongString\";\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       const char* x =\n"
                    "           \"veryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryVeryLongString\";\n"
                    "       ```",
            )]
            TemplateDeclarations: Annotated[
                Literal["Leave", "No", "Multiline", "Yes"],
                Field(
                    default="Yes",
                    examples=["Leave", "No", "Multiline", "Yes"],
                    description=
                        "Controls line breaking between template declarations and the "
                        "following declaration.\n"
                        "   `Leave`: preserve user formatting.\n"
                        "       ```cpp\n"
                        "       template <typename T>\n"
                        "       T foo() {\n"
                        "       }\n"
                        "       template <typename T> T foo(int aaaaaaaaaaaaaaaaaaaaa,\n"
                        "                                   int bbbbbbbbbbbbbbbbbbbbb) {\n"
                        "       }\n"
                        "       ```\n"
                        "   `No`: do not force a break before the declaration, unless "
                        "other formatting rules require it.\n"
                        "       ```cpp\n"
                        "       template <typename T> T foo() {\n"
                        "       }\n"
                        "       template <typename T> T foo(int aaaaaaaaaaaaaaaaaaaaa,\n"
                        "                                   int bbbbbbbbbbbbbbbbbbbbb) {\n"
                        "       }\n"
                        "       ```\n"
                        "   `Multiline`: force a break when the following declaration "
                        "spans multiple lines.\n"
                        "       ```cpp\n"
                        "       template <typename T> T foo() {\n"
                        "       }\n"
                        "       template <typename T>\n"
                        "       T foo(int aaaaaaaaaaaaaaaaaaaaa,\n"
                        "             int bbbbbbbbbbbbbbbbbbbbb) {\n"
                        "       }\n"
                        "       ```\n"
                        "   `Yes`: always break after the template declaration.\n"
                        "       ```cpp\n"
                        "       template <typename T>\n"
                        "       T foo() {\n"
                        "       }\n"
                        "       template <typename T>\n"
                        "       T foo(int aaaaaaaaaaaaaaaaaaaaa,\n"
                        "             int bbbbbbbbbbbbbbbbbbbbb) {\n"
                        "       }\n"
                        "       ```",
                )
            ]

        Break: Annotated[_Break, Field(
            default_factory=_Break.model_construct,
            description="Options for controlling line breaks.",
        )]

        class _BraceWrapping(BaseModel):
            """Validate the `[tool.clang-format.BraceWrapping]` table."""
            model_config = ConfigDict(extra="forbid")
            AfterCaseLabel: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap braces after case labels.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       switch (foo) {\n"
                    "           case 1:\n"
                    "           {\n"
                    "               bar();\n"
                    "               break;\n"
                    "           }\n"
                    "           default:\n"
                    "           {\n"
                    "               plop();\n"
                    "           }\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       switch (foo) {\n"
                    "           case 1: {\n"
                    "               bar();\n"
                    "               break;\n"
                    "           }\n"
                    "           default: {\n"
                    "               plop();\n"
                    "           }\n"
                    "       }\n"
                    "       ```",
            )]
            AfterClass: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap braces after class definitions.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       class foo\n"
                    "       {};\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       class foo {};\n"
                    "       ```",
            )]
            AfterControlStatement: Annotated[Literal["Never", "Multiline", "Always"], Field(
                default="Never",
                examples=["Never", "Multiline", "Always"],
                description=
                    "Controls whether to wrap braces after control statements "
                    "(`if`/`for`/`while`/`switch`/...).\n"
                    "   `Never`: never wrap braces after a control statement.\n"
                    "       ```cpp\n"
                    "       if (foo()) {\n"
                    "       } else {\n"
                    "       }\n"
                    "       for (int i = 0; i < 10; ++i) {\n"
                    "       }\n"
                    "       ```\n"
                    "   `Multiline`: only wrap braces after a multi-line control statement.\n"
                    "       ```cpp\n"
                    "       if (foo && bar &&\n"
                    "           baz)\n"
                    "       {\n"
                    "           quux();\n"
                    "       }\n"
                    "       while (foo || bar) {\n"
                    "       }\n"
                    "       ```\n"
                    "   `Always`: always wrap braces after a control statement.\n"
                    "       ```cpp\n"
                    "       if (foo())\n"
                    "       {\n"
                    "       } else\n"
                    "       {}\n"
                    "       for (int i = 0; i < 10; ++i)\n"
                    "       {}\n"
                    "       ```",
            )]
            AfterEnum: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap braces after enum definitions.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       enum X : int\n"
                    "       {\n"
                    "           B\n"
                    "       };\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       enum X : int { B };\n"
                    "       ```",
            )]
            AfterFunction: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap braces after function definitions.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       void foo()\n"
                    "       {\n"
                    "           bar();\n"
                    "           bar2();\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       void foo() {\n"
                    "           bar();\n"
                    "           bar2();\n"
                    "       }\n"
                    "       ```",
            )]
            AfterNamespace: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap braces after namespace definitions.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       namespace\n"
                    "       {\n"
                    "       int foo();\n"
                    "       int bar();\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       namespace {\n"
                    "       int foo();\n"
                    "       int bar();\n"
                    "       }\n"
                    "       ```",
            )]
            AfterStruct: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap braces after struct definitions.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       struct foo\n"
                    "       {\n"
                    "           int x;\n"
                    "       };\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       struct foo {\n"
                    "           int x;\n"
                    "       };\n"
                    "       ```",
            )]
            AfterUnion: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap braces after union definitions.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       union foo\n"
                    "       {\n"
                    "           int x;\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       union foo {\n"
                    "           int x;\n"
                    "       }\n"
                    "       ```",
            )]
            AfterExternBlock: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap braces after extern blocks.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       extern \"C\"\n"
                    "       {\n"
                    "           int foo();\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       extern \"C\" {\n"
                    "       int foo();\n"
                    "       }\n"
                    "       ```",
            )]
            BeforeCatch: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap before `catch`.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       try {\n"
                    "           foo();\n"
                    "       }\n"
                    "       catch () {\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       try {\n"
                    "           foo();\n"
                    "       } catch () {\n"
                    "       }\n"
                    "       ```",
            )]
            BeforeElse: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap before `else`.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       if (foo()) {\n"
                    "       }\n"
                    "       else {\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       if (foo()) {\n"
                    "       } else {\n"
                    "       }\n"
                    "       ```",
            )]
            BeforeLambdaBody: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap braces in lambda bodies.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       connect(\n"
                    "           []()\n"
                    "           {\n"
                    "               foo();\n"
                    "               bar();\n"
                    "           });\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       connect([]() {\n"
                    "           foo();\n"
                    "           bar();\n"
                    "       });\n"
                    "       ```",
            )]
            BeforeWhile: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to wrap before `while`.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       do {\n"
                    "           foo();\n"
                    "       }\n"
                    "       while (1);\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       do {\n"
                    "           foo();\n"
                    "       } while (1);\n"
                    "       ```",
            )]
            IndentBraces: Annotated[bool, Field(
                default=False,
                description="Controls whether wrapped braces are indented.",
            )]
            SplitEmptyFunction: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether empty function bodies can be put on a single "
                    "line.  This option is used only if the opening brace of the function "
                    "has already been wrapped (i.e. `AfterFunction` is enabled), and "
                    "other rules indicate the function could/should not be put on a "
                    "single line.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       int f()\n"
                    "       {\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       int f()\n"
                    "       {}\n"
                    "       ```",
            )]
            SplitEmptyRecord: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether empty records (e.g. class, struct, or union) can "
                    "be put on a single line.  This option is used only if the opening "
                    "brace of the record has already been wrapped (i.e. `AfterClass` "
                    "for classes is enabled).\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       class Foo\n"
                    "       {\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       class Foo\n"
                    "       {}\n"
                    "       ```",
            )]
            SplitEmptyNamespace: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether empty namespace bodies can be put on a single "
                    "line.  This option is used only if the opening brace of the "
                    "namespace has already been wrapped (i.e. `AfterNamespace` is "
                    "enabled).\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       namespace Foo\n"
                    "       {\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       namespace Foo\n"
                    "       {}\n"
                    "       ```",
            )]

        BraceWrapping: Annotated[_BraceWrapping, Field(
            default_factory=_BraceWrapping.model_construct,
            description="Options for fine-grained brace wrapping.",
        )]

        class _Indent(BaseModel):
            """Validate the `[tool.clang-format.Indent]` table."""
            model_config = ConfigDict(extra="forbid")
            CaseLabels: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to indent case labels one level from the switch "
                    "statement.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       switch (fool) {\n"
                    "       case 1:\n"
                    "           bar();\n"
                    "           break;\n"
                    "       default:\n"
                    "           plop();\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       switch (fool) {\n"
                    "       case 1:\n"
                    "           bar();\n"
                    "           break;\n"
                    "       default:\n"
                    "           plop();\n"
                    "       }\n"
                    "       ```",
            )]
            ExportBlock: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to indent the body of an `export { ... }` block.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       export {\n"
                    "           void foo();\n"
                    "           void bar();\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       export {\n"
                    "       void foo();\n"
                    "       void bar();\n"
                    "       }\n"
                    "       ```",
            )]
            ExternBlock: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to indent extern blocks.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       extern \"C\" {\n"
                    "           void foo();\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       extern \"C\" {\n"
                    "       void foo();\n"
                    "       }\n"
                    "       ```",
            )]
            GotoLabels: Annotated[
                Literal["NoIndent", "OuterIndent", "InnerIndent", "HalfIndent"],
                Field(
                    default="InnerIndent",
                    examples=["NoIndent", "OuterIndent", "InnerIndent", "HalfIndent"],
                    description=
                        "Controls how goto labels are indented.\n"
                        "   `NoIndent`: do not indent goto labels.\n"
                        "       ```cpp\n"
                        "       int f() {\n"
                        "           if (foo()) {\n"
                        "           label1:\n"
                        "               bar();\n"
                        "           }\n"
                        "       label2:\n"
                        "           return 1;\n"
                        "       }\n"
                        "       ```\n"
                        "   `OuterIndent`: indent goto labels one level from the "
                        "surrounding block.\n"
                        "       ```cpp\n"
                        "       int f() {\n"
                        "           if (foo()) {\n"
                        "               label1:\n"
                        "               bar();\n"
                        "           }\n"
                        "           label2:\n"
                        "           return 1;\n"
                        "       }\n"
                        "       ```\n"
                        "   `InnerIndent`: indent goto labels one level and "
                        "statements under labels one additional level.\n"
                        "       ```cpp\n"
                        "       int f() {\n"
                        "           if (foo()) {\n"
                        "               label1:\n"
                        "                   bar();\n"
                        "           }\n"
                        "           label2:\n"
                        "               return 1;\n"
                        "       }\n"
                        "       ```\n"
                        "   `HalfIndent`: indent goto labels by half an indent "
                        "width.\n"
                        "       ```cpp\n"
                        "       int f() {\n"
                        "           if (foo()) {\n"
                        "             label1:\n"
                        "               bar();\n"
                        "           }\n"
                        "         label2:\n"
                        "           return 1;\n"
                        "       }\n"
                        "       ```",
                )
            ]
            PPDirectives: Annotated[
                Literal["None", "AfterHash", "BeforeHash", "Leave"],
                Field(
                    default="BeforeHash",
                    examples=["None", "AfterHash", "BeforeHash", "Leave"],
                    description=
                        "Controls preprocessor directive indentation style.\n"
                        "   `None`: do not indent any directives.\n"
                        "       ```cpp\n"
                        "       #if FOO\n"
                        "       #if BAR\n"
                        "       #include <foo>\n"
                        "       #endif\n"
                        "       #endif\n"
                        "       ```\n"
                        "   `AfterHash`: indent directives after the hash.\n"
                        "       ```cpp\n"
                        "       #if FOO\n"
                        "       #   if BAR\n"
                        "       #       include <foo>\n"
                        "       #   endif\n"
                        "       #endif\n"
                        "       ```\n"
                        "   `BeforeHash`: indent directives before the hash.\n"
                        "       ```cpp\n"
                        "       #if FOO\n"
                        "           #if BAR\n"
                        "               #include <foo>\n"
                        "           #endif\n"
                        "       #endif\n"
                        "       ```\n"
                        "   `Leave`: preserve user formatting.\n"
                        "       ```cpp\n"
                        "       #if FOO\n"
                        "           #if BAR\n"
                        "       #include <foo>\n"
                        "           #endif\n"
                        "       #endif\n"
                        "       ```",
                )
            ]
            RequiresClause: Annotated[bool, Field(
                default=True,
                description=
                    "Controls whether to indent the requires clause in a template.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       template <typename It>\n"
                    "           requires Iterator<It>\n"
                    "       void sort(It begin, It end) {\n"
                    "           //....\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       template <typename It>\n"
                    "       requires Iterator<It>\n"
                    "       void sort(It begin, It end) {\n"
                    "           //....\n"
                    "       }\n"
                    "       ```",
            )]
            Width: Annotated[NonNegativeInt, Field(
                default=4,
                description=
                    "Controls the number of columns to use for indentation.  For example, "
                    "if set to 3:\n"
                    "   ```cpp\n"
                    "   void f() {\n"
                    "      someFunction();\n"
                    "      if (true, false) {\n"
                    "         f();\n"
                    "      }\n"
                    "   }\n"
                    "   ```",
            )]
            WrappedFunctionNames: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to indent function names when a declaration or "
                    "definition wraps after the return type.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       LoooooooooooooooooooooooooooooooooooooooongReturnType\n"
                    "           LoooooooooooooooooooooooooooooooongFunctionDeclaration();\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       LoooooooooooooooooooooooooooooooooooooooongReturnType\n"
                    "       LoooooooooooooooooooooooooooooooongFunctionDeclaration();\n"
                    "       ```",
            )]

        Indent: Annotated[_Indent, Field(
            default_factory=_Indent.model_construct,
            description="Options for controlling indentation.",
        )]

        class _IntegerLiteralSeparator(BaseModel):
            """Validate the `[tool.clang-format.IntegerLiteralSeparator]` table."""
            model_config = ConfigDict(extra="forbid")

            class _Base(BaseModel):
                """Validate a `[tool.clang-format.IntegerLiteralSeparator.<base>]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                Every: Annotated[int, Field(
                    default=0,
                    ge=-1,
                    description=
                        "Controls how often separators are inserted for this base.  "
                        "`-1` removes all separators, `0` preserves existing "
                        "separators, and positive values insert separators from the "
                        "rightmost digit.\n"
                        "   ```cpp\n"
                        "   /* -1: */ b = 0b100111101101;\n"
                        "   /*  0: */ b = 0b10011'11'0110'1;\n"
                        "   /*  3: */ b = 0b100'111'101'101;\n"
                        "   /*  4: */ b = 0b1001'1110'1101;\n"
                        "   ```",
                )]
                Min: Annotated[int, Field(
                    default=-1,
                    ge=-1,
                    description=
                        "Controls the minimum number of digits required before "
                        "separators are inserted.  `-1` disables this threshold.\n"
                        "   ```cpp\n"
                        "   // Every: 3\n"
                        "   // Min: 7\n"
                        "   b1 = 0b101101;\n"
                        "   b2 = 0b1'101'101;\n"
                        "   ```",
                )]
                Max: Annotated[int, Field(
                    default=-1,
                    ge=-1,
                    description=
                        "Controls the maximum number of digits for which separators "
                        "are removed.  `-1` disables this threshold.\n"
                        "   ```cpp\n"
                        "   // Every: 3\n"
                        "   // Min: 7\n"
                        "   // Max: 4\n"
                        "   b0 = 0b1011; // Always removed.\n"
                        "   b1 = 0b101101; // Not added.\n"
                        "   b2 = 0b1'01'101; // Not removed, not corrected.\n"
                        "   b3 = 0b1'101'101; // Always added.\n"
                        "   b4 = 0b10'1101; // Corrected to 0b101'101.\n"
                        "   ```",
                )]

                @model_validator(mode="after")
                def _validate_min_max(self) -> Self:
                    if self.Min != -1 and self.Max != -1 and self.Max <= self.Min:
                        raise ValueError("Max must be greater than Min when both are set.")
                    return self

            Binary: Annotated[_Base, Field(
                default_factory=_Base.model_construct,
                description="Options for formatting separators in binary literals.",
            )]
            Decimal: Annotated[_Base, Field(
                default_factory=_Base.model_construct,
                description="Options for formatting separators in decimal literals.",
            )]
            Hex: Annotated[_Base, Field(
                default_factory=_Base.model_construct,
                description="Options for formatting separators in hexadecimal literals.",
            )]

        IntegerLiteralSeparator: Annotated[_IntegerLiteralSeparator, Field(
            default_factory=_IntegerLiteralSeparator.model_construct,
            description="Options for formatting integer literal separators.",
        )]

        class _KeepEmptyLines(BaseModel):
            """Validate the `[tool.clang-format.KeepEmptyLines]` table."""
            model_config = ConfigDict(extra="forbid")
            AtEndOfFile: Annotated[bool, Field(
                default=False,
                description="Controls whether to keep empty lines at end of file.",
            )]
            AtStartOfBlock: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether to keep empty lines at start of a block.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       if (foo) {\n"
                    "\n"
                    "           bar();\n"
                    "       }\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       if (foo) {\n"
                    "           bar();\n"
                    "       }\n"
                    "       ```",
            )]
            AtStartOfFile: Annotated[bool, Field(
                default=False,
                description="Controls whether to keep empty lines at start of file.",
            )]

        KeepEmptyLines: Annotated[_KeepEmptyLines, Field(
            default_factory=_KeepEmptyLines.model_construct,
            description="Options for explicitly keeping or removing empty lines.",
        )]

        class _NumericLiteralCase(BaseModel):
            """Validate the `[tool.clang-format.NumericLiteralCase]` table."""
            model_config = ConfigDict(extra="forbid")
            ExponentLetter: Annotated[Literal["Leave", "Upper", "Lower"], Field(
                default="Lower",
                examples=["Leave", "Upper", "Lower"],
                description=
                    "Controls floating point exponent separator letter case.\n"
                    "   `Leave`: preserve user formatting.\n"
                    "       ```cpp\n"
                    "       float a = 6.02e23 + 1.0E10;\n"
                    "       ```\n"
                    "   `Upper`: format this component with uppercase characters.\n"
                    "       ```cpp\n"
                    "       float a = 6.02E23 + 1.0E10;\n"
                    "       ```\n"
                    "   `Lower`: format this component with lowercase characters.\n"
                    "       ```cpp\n"
                    "       float a = 6.02e23 + 1.0e10;\n"
                    "       ```",
            )]
            HexDigit: Annotated[Literal["Leave", "Upper", "Lower"], Field(
                default="Upper",
                examples=["Leave", "Upper", "Lower"],
                description=
                    "Controls hexadecimal digit case.\n"
                    "   `Leave`: preserve user formatting.\n"
                    "       ```cpp\n"
                    "       a = 0xaBcDeF;\n"
                    "       ```\n"
                    "   `Upper`: format this component with uppercase characters.\n"
                    "       ```cpp\n"
                    "       a = 0xABCDEF;\n"
                    "       ```\n"
                    "   `Lower`: format this component with lowercase characters.\n"
                    "       ```cpp\n"
                    "       a = 0xabcdef;\n"
                    "       ```",
            )]
            Prefix: Annotated[Literal["Leave", "Upper", "Lower"], Field(
                default="Lower",
                examples=["Leave", "Upper", "Lower"],
                description=
                    "Controls integer prefix case.\n"
                    "   `Leave`: preserve user formatting.\n"
                    "       ```cpp\n"
                    "       a = 0XF0 | 0b1;\n"
                    "       ```\n"
                    "   `Upper`: format this component with uppercase characters.\n"
                    "       ```cpp\n"
                    "       a = 0XF0 | 0B1;\n"
                    "       ```\n"
                    "   `Lower`: format this component with lowercase characters.\n"
                    "       ```cpp\n"
                    "       a = 0xF0 | 0b1;\n"
                    "       ```",
            )]
            Suffix: Annotated[Literal["Leave", "Upper", "Lower"], Field(
                default="Lower",
                examples=["Leave", "Upper", "Lower"],
                description=
                    "Controls suffix case.  This excludes case-sensitive reserved "
                    "suffixes, such as `min` in C++.\n"
                    "   `Leave`: preserve user formatting.\n"
                    "       ```cpp\n"
                    "       a = 1uLL;\n"
                    "       b = 1Ull;\n"
                    "       ```\n"
                    "   `Upper`: format this component with uppercase characters.\n"
                    "       ```cpp\n"
                    "       a = 1ULL;\n"
                    "       b = 1ULL;\n"
                    "       ```\n"
                    "   `Lower`: format this component with lowercase characters.\n"
                    "       ```cpp\n"
                    "       a = 1ull;\n"
                    "       b = 1ull;\n"
                    "       ```",
            )]

        NumericLiteralCase: Annotated[_NumericLiteralCase, Field(
            default_factory=_NumericLiteralCase.model_construct,
            description="Options for capitalization style of numeric literals.",
        )]

        class _SortIncludes(BaseModel):
            """Validate the `[tool.clang-format.SortIncludes]` table."""
            model_config = ConfigDict(extra="forbid")
            Enabled: Annotated[bool, Field(
                default=True,
                description="Controls whether include sorting is applied.",
            )]
            IgnoreCase: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether includes are sorted case-insensitively.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       #include \"A/B.h\"\n"
                    "       #include \"A/b.h\"\n"
                    "       #include \"a/b.h\"\n"
                    "       #include \"B/A.h\"\n"
                    "       #include \"B/a.h\"\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       #include \"A/B.h\"\n"
                    "       #include \"A/b.h\"\n"
                    "       #include \"B/A.h\"\n"
                    "       #include \"B/a.h\"\n"
                    "       #include \"a/b.h\"\n"
                    "       ```",
            )]
            IgnoreExtension: Annotated[bool, Field(
                default=False,
                description=
                    "Controls whether file extensions are considered only when two "
                    "includes compare equal otherwise.\n"
                    "   `true`:\n"
                    "       ```cpp\n"
                    "       #include \"A.h\"\n"
                    "       #include \"A.inc\"\n"
                    "       #include \"A-util.h\"\n"
                    "       ```\n"
                    "   `false`:\n"
                    "       ```cpp\n"
                    "       #include \"A-util.h\"\n"
                    "       #include \"A.h\"\n"
                    "       #include \"A.inc\"\n"
                    "       ```",
            )]

        SortIncludes: Annotated[_SortIncludes, Field(
            default_factory=_SortIncludes.model_construct,
            description="Options for if and how includes are sorted.",
        )]

        class _Space(BaseModel):
            """Validate the `[tool.clang-format.Space]` table."""
            model_config = ConfigDict(extra="forbid")
            AfterCStyleCast: Annotated[bool, Field(
                default=False
            )]
            AfterLogicalNot: Annotated[bool, Field(
                default=False
            )]
            AfterOperatorKeyword: Annotated[bool, Field(
                default=False
            )]
            AfterTemplateKeyword: Annotated[bool, Field(
                default=False
            )]
            BeforeAssignmentOperators: Annotated[bool, Field(
                default=True
            )]
            BeforeCaseColon: Annotated[bool, Field(
                default=False
            )]
            BeforeCpp11BracedList: Annotated[bool, Field(
                default=False
            )]
            BeforeCtorInitializerColon: Annotated[bool, Field(
                default=True
            )]
            BeforeInheritanceColon: Annotated[bool, Field(
                default=True
            )]
            BeforeJsonColon: Annotated[bool, Field(
                default=False
            )]
            BeforeRangeBasedForLoopColon: Annotated[bool, Field(
                default=True
            )]
            BeforeSquareBrackets: Annotated[bool, Field(
                default=False
            )]
            InEmptyBraces: Annotated[Literal["Never", "Block", "Always"], Field(
                default="Never",
                examples=["Never", "Block", "Always"],
            )]

            class _BeforeParensOptions(BaseModel):
                """Validate the `[tool.clang-format.Space.BeforeParensOptions]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                AfterControlStatements: Annotated[bool, Field(
                    default=True
                )]
                AfterForeachMacros: Annotated[bool, Field(
                    default=True
                )]
                AfterFunctionDeclarationName: Annotated[bool, Field(
                    default=False
                )]
                AfterFunctionDefinitionName: Annotated[bool, Field(
                    default=False
                )]
                AfterIfMacros: Annotated[bool, Field(
                    default=True
                )]
                AfterNot: Annotated[bool, Field(
                    default=True
                )]
                AfterOverloadedOperator: Annotated[bool, Field(
                    default=False
                )]
                AfterPlacementOperator: Annotated[bool, Field(
                    default=True
                )]
                AfterRequiresInClause: Annotated[bool, Field(
                    default=False
                )]
                AfterRequiresInExpression: Annotated[bool, Field(
                    default=False
                )]
                BeforeNonEmptyParentheses: Annotated[bool, Field(
                    default=False
                )]

            BeforeParensOptions: Annotated[_BeforeParensOptions, Field(
                default_factory=_BeforeParensOptions.model_construct
            )]

            class _InParensOptions(BaseModel):
                """Validate the `[tool.clang-format.Space.InParensOptions]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                ExceptDoubleParentheses: Annotated[bool, Field(
                    default=False
                )]
                InConditionalStatements: Annotated[bool, Field(
                    default=False
                )]
                InCStyleCasts: Annotated[bool, Field(
                    default=False
                )]
                InEmptyParentheses: Annotated[bool, Field(
                    default=False
                )]
                Other: Annotated[bool, Field(
                    default=False
                )]

            InParensOptions: Annotated[_InParensOptions, Field(
                default_factory=_InParensOptions.model_construct
            )]

        Space: Annotated[_Space, Field(
            default_factory=_Space.model_construct
        )]

    async def init(self, config: Config, cli: Config.Init) -> dict[str, Any]:
        return self.Model.model_construct().model_dump(by_alias=True)

    async def validate(self, config: Config, fragment: Any) -> Model | None:
        return self.Model.model_validate(fragment)

    def _integer_literal_separator(self, name: str) -> dict[str, int]:
        model = getattr(self.Model.IntegerLiteralSeparator, name)
        result = {name: model.Every}
        if model.Min != -1:
            result[f"{name}MinDigitsInsert"] = model.Min
        if model.Max != -1:
            result[f"{name}MaxDigitsRemove"] = model.Max
        return result

    async def render(self, config: Config, tag: str | None) -> None:
        model = config.get(ClangFormat)
        if tag is None or model is None:
            return
        content: dict[str, Any] = {
            "DisableFormat": not model.Enable,
            "BasedOnStyle": "Mozilla",  # shouldn't matter because our config is exhaustive
            "AccessModifierOffset": model.AccessModifierOffset,
            "AlignAfterOpenBracket": model.Align.AfterOpenBracket,
            "AlignArrayOfStructures": model.Align.ArrayOfStructures,
            "AlignEscapedNewlines": model.Align.EscapedNewlines,
            "AlignOperands": model.Align.Operands,
            "AlignConsecutiveAssignments": {
                "Enabled": model.Align.ConsecutiveAssignments.Enabled,
                "AcrossEmptyLines": model.Align.ConsecutiveAssignments.AcrossEmptyLines,
                "AcrossComments": model.Align.ConsecutiveAssignments.AcrossComments,
                "AlignCompound": model.Align.ConsecutiveAssignments.AlignCompound,
                "PadOperators": model.Align.ConsecutiveAssignments.PadOperators,
            },
            "AlignConsecutiveBitFields": {
                "Enabled": model.Align.ConsecutiveBitFields.Enabled,
                "AcrossEmptyLines": model.Align.ConsecutiveBitFields.AcrossEmptyLines,
                "AcrossComments": model.Align.ConsecutiveBitFields.AcrossComments,
            },
            "AlignConsecutiveDeclarations": {
                "Enabled": model.Align.ConsecutiveDeclarations.Enabled,
                "AcrossEmptyLines": model.Align.ConsecutiveDeclarations.AcrossEmptyLines,
                "AcrossComments": model.Align.ConsecutiveDeclarations.AcrossComments,
                "AlignFunctionDeclarations":
                    model.Align.ConsecutiveDeclarations.AlignFunctionDeclarations,
                "AlignFunctionPointers":
                    model.Align.ConsecutiveDeclarations.AlignFunctionPointers,
            },
            "AlignConsecutiveMacros": {
                "Enabled": model.Align.ConsecutiveMacros.Enabled,
                "AcrossEmptyLines": model.Align.ConsecutiveMacros.AcrossEmptyLines,
                "AcrossComments": model.Align.ConsecutiveMacros.AcrossComments,
            },
            "AlignConsecutiveShortCaseStatements": {
                "Enabled": model.Align.ConsecutiveShortCaseStatements.Enabled,
                "AcrossEmptyLines": model.Align.ConsecutiveShortCaseStatements.AcrossEmptyLines,
                "AcrossComments": model.Align.ConsecutiveShortCaseStatements.AcrossComments,
                "AlignCaseArrows": model.Align.ConsecutiveShortCaseStatements.AlignCaseArrows,
                "AlignCaseColons": model.Align.ConsecutiveShortCaseStatements.AlignCaseColons,
            },
            "AlignTrailingComments": {
                "Kind": model.Align.TrailingComments.Kind,
                "OverEmptyLines": model.Align.TrailingComments.OverEmptyLines,
                "AlignPPAndNotPP": model.Align.TrailingComments.AlignPPAndNotPP,
            },
            "AllowAllArgumentsOnNextLine": model.Allow.AllArgumentsOnNextLine,
            "AllowAllParametersOfDeclarationOnNextLine":
                model.Allow.AllParametersOfDeclarationOnNextLine,
            "AllowBreakBeforeNoexceptSpecifier": model.Allow.BreakBeforeNoexceptSpecifier,
            "AllowShortBlocksOnASingleLine": model.Allow.ShortBlocksOnASingleLine,
            "AllowShortCaseExpressionsOnASingleLine":
                model.Allow.ShortCaseExpressionsOnASingleLine,
            "AllowShortCaseLabelsOnASingleLine": model.Allow.ShortCaseLabelsOnASingleLine,
            "AllowShortCompoundRequirementsOnASingleLine":
                model.Allow.ShortCompoundRequirementsOnASingleLine,
            "AllowShortEnumsOnASingleLine": model.Allow.ShortEnumsOnASingleLine,
            "AllowShortFunctionsOnASingleLine": {
                "Empty": model.Allow.ShortFunctionsOnASingleLine.Empty,
                "Inline": model.Allow.ShortFunctionsOnASingleLine.Inline,
                "Other": model.Allow.ShortFunctionsOnASingleLine.Other,
            },
            "AllowShortIfStatementsOnASingleLine": model.Allow.ShortIfStatementsOnASingleLine,
            "AllowShortLambdasOnASingleLine": model.Allow.ShortLambdasOnASingleLine,
            "AllowShortLoopsOnASingleLine": model.Allow.ShortLoopsOnASingleLine,
            "AllowShortNamespacesOnASingleLine": model.Allow.ShortNamespacesOnASingleLine,
            "AlwaysBreakBeforeMultilineStrings": model.AlwaysBreakBeforeMultilineStrings,
            "AttributeMacros": model.AttributeMacros,
            "BinPackArguments": model.BinPackArguments,
            "BinPackLongBracedList": model.BinPackLongBracedList,
            "BinPackParameters": model.BinPackParameters,
            "BitFieldColonSpacing": model.BitFieldColonSpacing,
            "BreakAdjacentStringLiterals": model.Break.AdjacentStringLiterals,
            "BreakAfterAttributes": model.Break.AfterAttributes,
            "BreakAfterOpenBracketBracedList": model.Break.AfterOpenBracketBracedList,
            "BreakAfterOpenBracketFunction": model.Break.AfterOpenBracketFunction,
            "BreakAfterOpenBracketIf": model.Break.AfterOpenBracketIf,
            "BreakAfterOpenBracketLoop": model.Break.AfterOpenBracketLoop,
            "BreakAfterOpenBracketSwitch": model.Break.AfterOpenBracketSwitch,
            "BreakAfterReturnType": model.Break.AfterReturnType,
            "BreakBeforeBinaryOperators": model.Break.BeforeBinaryOperators,
            "BreakBeforeBraces": "Custom",  # defer to BraceWrapping
            "BreakBeforeCloseBracketBracedList": model.Break.BeforeCloseBracketBracedList,
            "BreakBeforeCloseBracketFunction": model.Break.BeforeCloseBracketFunction,
            "BreakBeforeCloseBracketIf": model.Break.BeforeCloseBracketIf,
            "BreakBeforeCloseBracketLoop": model.Break.BeforeCloseBracketLoop,
            "BreakBeforeCloseBracketSwitch": model.Break.BeforeCloseBracketSwitch,
            "BreakBeforeConceptDeclarations": model.Break.BeforeConceptDeclarations,
            "BreakBeforeInlineASMColon": model.Break.BeforeInlineASMColon,
            "BreakBeforeTemplateCloser": model.Break.BeforeTemplateCloser,
            "BreakBeforeTernaryOperators": model.Break.BeforeTernaryOperators,
            "BreakBinaryOperations": model.Break.BinaryOperations,
            "BreakConstructorInitializers": model.Break.ConstructorInitializers,
            "BreakFunctionDefinitionParameters": model.Break.FunctionDefinitionParameters,
            "BreakInheritanceList": model.Break.InheritanceList,
            "BreakStringLiterals": model.Break.StringLiterals,
            "BreakTemplateDeclarations": model.Break.TemplateDeclarations,
            "BraceWrapping": {
                "AfterCaseLabel": model.BraceWrapping.AfterCaseLabel,
                "AfterClass": model.BraceWrapping.AfterClass,
                "AfterControlStatement": model.BraceWrapping.AfterControlStatement,
                "AfterEnum": model.BraceWrapping.AfterEnum,
                "AfterFunction": model.BraceWrapping.AfterFunction,
                "AfterNamespace": model.BraceWrapping.AfterNamespace,
                "AfterStruct": model.BraceWrapping.AfterStruct,
                "AfterUnion": model.BraceWrapping.AfterUnion,
                "AfterExternBlock": model.BraceWrapping.AfterExternBlock,
                "BeforeCatch": model.BraceWrapping.BeforeCatch,
                "BeforeElse": model.BraceWrapping.BeforeElse,
                "BeforeLambdaBody": model.BraceWrapping.BeforeLambdaBody,
                "BeforeWhile": model.BraceWrapping.BeforeWhile,
                "IndentBraces": model.BraceWrapping.IndentBraces,
                "SplitEmptyFunction": model.BraceWrapping.SplitEmptyFunction,
                "SplitEmptyRecord": model.BraceWrapping.SplitEmptyRecord,
                "SplitEmptyNamespace": model.BraceWrapping.SplitEmptyNamespace,
            },
            "ColumnLimit": model.ColumnLimit,
            "CompactNamespaces": model.CompactNamespaces,
            "Cpp11BracedListStyle": "FunctionCall",  # always use C++11 braced style
            "EmptyLineAfterAccessModifier": model.EmptyLineAfterAccessModifier,
            "EmptyLineBeforeAccessModifier": model.EmptyLineBeforeAccessModifier,
            "FixNamespaceComments": model.FixNamespaceComments,
            "ForEachMacros": model.ForEachMacros,
            "IfMacros": model.IfMacros,
            "IncludeBlocks": model.IncludeBlocks,
            "IndentCaseLabels": model.Indent.CaseLabels,
            "IndentExportBlock": model.Indent.ExportBlock,
            "IndentExternBlock": "Indent" if model.Indent.ExternBlock else "NoIndent",
            "IndentGotoLabels": model.Indent.GotoLabels,
            "IndentPPDirectives": model.Indent.PPDirectives,
            "IndentRequiresClause": model.Indent.RequiresClause,
            "IndentWidth": model.Indent.Width,
            "IndentWrappedFunctionNames": model.Indent.WrappedFunctionNames,
            "InsertBraces": model.InsertBraces,
            "InsertNewlineAtEOF": model.InsertNewlineAtEOF,
            "IntegerLiteralSeparator": (
                self._integer_literal_separator("Binary") |
                self._integer_literal_separator("Decimal") |
                self._integer_literal_separator("Hex")
            ),
            "KeepEmptyLines": {
                "AtEndOfFile": model.KeepEmptyLines.AtEndOfFile,
                "AtStartOfBlock": model.KeepEmptyLines.AtStartOfBlock,
                "AtStartOfFile": model.KeepEmptyLines.AtStartOfFile,
            },
            "LineEnding": model.LineEnding,
            "NamespaceIndentation": model.NamespaceIndentation,
            "NamespaceMacros": model.NamespaceMacros,
            "NumericLiteralCase": {
                "ExponentLetter": model.NumericLiteralCase.ExponentLetter,
                "HexDigit": model.NumericLiteralCase.HexDigit,
                "Prefix": model.NumericLiteralCase.Prefix,
                "Suffix": model.NumericLiteralCase.Suffix,
            },
            "OneLineFormatOffRegex": model.OneLineFormatOffRegex,
            "PackConstructorInitializers": model.PackConstructorInitializers,
            "PointerAlignment": model.PointerAlignment,
            "QualifierAlignment": "Custom",  # defer to QualifierOrder
            "QualifierOrder": model.QualifierOrder,
            "ReferenceAlignment": model.ReferenceAlignment,
            "ReflowComments": model.ReflowComments,
            "RemoveEmptyLinesInUnwrappedLines": model.RemoveEmptyLinesInUnwrappedLines,
            "RequiresClausePosition": model.RequiresClausePosition,
            "RequiresExpressionIndentation": model.RequiresExpressionIndentation,
            "SeparateDefinitionBlocks": model.SeparateDefinitionBlocks,
            "SortIncludes": {
                "Enabled": model.SortIncludes.Enabled,
                "IgnoreCase": model.SortIncludes.IgnoreCase,
                "IgnoreExtension": model.SortIncludes.IgnoreExtension,
            },
            "SortUsingDeclarations": model.SortUsingDeclarations,
            "SpaceAfterCStyleCast": model.Space.AfterCStyleCast,
            "SpaceAfterLogicalNot": model.Space.AfterLogicalNot,
            "SpaceAfterOperatorKeyword": model.Space.AfterOperatorKeyword,
            "SpaceAfterTemplateKeyword": model.Space.AfterTemplateKeyword,
            "SpaceBeforeAssignmentOperators": model.Space.BeforeAssignmentOperators,
            "SpaceBeforeCaseColon": model.Space.BeforeCaseColon,
            "SpaceBeforeCpp11BracedList": model.Space.BeforeCpp11BracedList,
            "SpaceBeforeCtorInitializerColon": model.Space.BeforeCtorInitializerColon,
            "SpaceBeforeInheritanceColon": model.Space.BeforeInheritanceColon,
            "SpaceBeforeJsonColon": model.Space.BeforeJsonColon,
            "SpaceBeforeParens": "Custom",  # defer to Space.BeforeParensOptions
            "SpaceBeforeParensOptions": {
                "AfterControlStatements":
                    model.Space.BeforeParensOptions.AfterControlStatements,
                "AfterForeachMacros":
                    model.Space.BeforeParensOptions.AfterForeachMacros,
                "AfterFunctionDeclarationName":
                    model.Space.BeforeParensOptions.AfterFunctionDeclarationName,
                "AfterFunctionDefinitionName":
                    model.Space.BeforeParensOptions.AfterFunctionDefinitionName,
                "AfterIfMacros": model.Space.BeforeParensOptions.AfterIfMacros,
                "AfterNot": model.Space.BeforeParensOptions.AfterNot,
                "AfterOverloadedOperator":
                    model.Space.BeforeParensOptions.AfterOverloadedOperator,
                "AfterPlacementOperator":
                    model.Space.BeforeParensOptions.AfterPlacementOperator,
                "AfterRequiresInClause":
                    model.Space.BeforeParensOptions.AfterRequiresInClause,
                "AfterRequiresInExpression":
                    model.Space.BeforeParensOptions.AfterRequiresInExpression,
                "BeforeNonEmptyParentheses":
                    model.Space.BeforeParensOptions.BeforeNonEmptyParentheses,
            },
            "SpaceBeforeRangeBasedForLoopColon": model.Space.BeforeRangeBasedForLoopColon,
            "SpaceBeforeSquareBrackets": model.Space.BeforeSquareBrackets,
            "SpacesBeforeTrailingComments": model.SpacesBeforeTrailingComments,
            "SpacesInAngles": model.SpacesInAngles,
            "SpacesInContainerLiterals": False,  # mostly applies to ObjC/JavaScript, etc.
            "SpacesInLineCommentPrefix": {
                "Minimum": model.SpacesInLineCommentPrefix.Minimum,
                "Maximum": model.SpacesInLineCommentPrefix.Maximum,
            },
            "SpacesInParens": "Custom",  # defer to Space.InParensOptions
            "SpacesInParensOptions": {
                "ExceptDoubleParentheses":
                    model.Space.InParensOptions.ExceptDoubleParentheses,
                "InConditionalStatements":
                    model.Space.InParensOptions.InConditionalStatements,
                "InCStyleCasts": model.Space.InParensOptions.InCStyleCasts,
                "InEmptyParentheses": model.Space.InParensOptions.InEmptyParentheses,
                "Other": model.Space.InParensOptions.Other,
            },
            "SpacesInSquareBrackets": model.SpacesInSquareBrackets,
            "Standard": "Auto",  # detect C++ standard from source files + compile db
            "TabWidth": model.TabWidth,
            "TemplateNames": model.TemplateNames,
            "TypeNames": model.TypeNames,
            "TypenameMacros": model.TypenameMacros,
            "UseTab": model.UseTab,
            "WrapNamespaceBodyWithEmptyLines": model.WrapNamespaceBodyWithEmptyLines,
        }
        atomic_write_text(
            CONTAINER_TMP_MOUNT / ".clang-format",
            dump_yaml(content, resource_name=self.name),
            encoding="utf-8",
        )
