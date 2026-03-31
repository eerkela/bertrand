"""TODO"""
from __future__ import annotations

from typing import Annotated, Any, Literal

from pydantic import (
    AfterValidator,
    BaseModel,
    ConfigDict,
    Field,
    NonNegativeInt,
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

        # TODO: continue documenting these options

        class _Allow(BaseModel):
            """Validate the `[tool.clang-format.Allow]` table."""
            model_config = ConfigDict(extra="forbid")
            AllArgumentsOnNextLine: Annotated[bool, Field(default=False)]
            AllParametersOfDeclarationOnNextLine: Annotated[bool, Field(default=False)]
            BreakBeforeNoexceptSpecifier: Annotated[
                Literal["Never", "OnlyWithParen", "Always"],
                Field(
                    default="OnlyWithParen",
                    examples=["Never", "OnlyWithParen", "Always"],
                )
            ]
            ShortBlocksOnASingleLine: Annotated[Literal["Never", "Empty", "Always"], Field(
                default="Empty",
                examples=["Never", "Empty", "Always"],
            )]
            ShortCaseExpressionsOnASingleLine: Annotated[bool, Field(default=True)]
            ShortCaseLabelsOnASingleLine: Annotated[bool, Field(default=True)]
            ShortCompoundRequirementsOnASingleLine: Annotated[bool, Field(default=True)]
            ShortEnumsOnASingleLine: Annotated[bool, Field(default=True)]
            ShortFunctionsOnASingleLine: Annotated[
                Literal["None", "InlineOnly", "Empty", "Inline", "All"],
                Field(
                    default="All",
                    examples=["None", "InlineOnly", "Empty", "Inline", "All"],
                )
            ]
            ShortIfStatementsOnASingleLine: Annotated[
                Literal["None", "WithoutElse", "OnlyFirstIf", "AllIfsAndElse"],
                Field(
                    default="WithoutElse",
                    examples=["None", "WithoutElse", "OnlyFirstIf", "AllIfsAndElse"],
                )
            ]
            ShortLambdasOnASingleLine: Annotated[
                Literal["None", "Empty", "Inline", "All"],
                Field(
                    default="All",
                    examples=["None", "Empty", "Inline", "All"],
                )
            ]
            ShortLoopsOnASingleLine: Annotated[bool, Field(default=True)]
            ShortNamespacesOnASingleLine: Annotated[bool, Field(default=False)]

        Allow: Annotated[_Allow, Field(
            default_factory=_Allow.model_construct
        )]

        class _Break(BaseModel):
            """Validate the `[tool.clang-format.Break]` table."""
            model_config = ConfigDict(extra="forbid")
            AdjacentStringLiterals: Annotated[bool, Field(default=True)]
            AfterAttributes: Annotated[Literal["Never", "Leave", "Always"], Field(
                default="Never",
                examples=["Never", "Leave", "Always"],
            )]
            AfterOpenBracketBracedList: Annotated[bool, Field(default=True)]
            AfterOpenBracketFunction: Annotated[bool, Field(default=True)]
            AfterOpenBracketIf: Annotated[bool, Field(default=True)]
            AfterOpenBracketLoop: Annotated[bool, Field(default=True)]
            AfterOpenBracketSwitch: Annotated[bool, Field(default=True)]
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
                )
            ]
            BeforeBinaryOperators: Annotated[Literal["None", "NonAssignment", "All"], Field(
                default="None",
                examples=["None", "NonAssignment", "All"],
            )]
            BeforeCloseBracketBracedList: Annotated[bool, Field(default=True)]
            BeforeCloseBracketFunction: Annotated[bool, Field(default=True)]
            BeforeCloseBracketIf: Annotated[bool, Field(default=True)]
            BeforeCloseBracketLoop: Annotated[bool, Field(default=True)]
            BeforeCloseBracketSwitch: Annotated[bool, Field(default=True)]
            BeforeConceptDeclarations: Annotated[Literal["Never", "Allowed", "Always"], Field(
                default="Always",
                examples=["Never", "Allowed", "Always"],
            )]
            BeforeInlineASMColon: Annotated[Literal["Never", "OnlyMultiline", "Always"], Field(
                default="OnlyMultiline",
                examples=["Never", "OnlyMultiline", "Always"],
            )]
            BeforeTemplateCloser: Annotated[bool, Field(default=True)]
            BeforeTernaryOperators: Annotated[bool, Field(default=False)]
            BinaryOperations: Annotated[
                Literal["Never", "OnePerLine", "RespectPrecedence"],
                Field(
                    default="Never",
                    examples=["Never", "OnePerLine", "RespectPrecedence"],
                )
            ]
            ConstructorInitializers: Annotated[
                Literal["BeforeColon", "AfterColon", "BeforeComma", "AfterComma"],
                Field(
                    default="AfterColon",
                    examples=["BeforeColon", "AfterColon", "BeforeComma", "AfterComma"],
                )
            ]
            FunctionDefinitionParameters: Annotated[bool, Field(default=False)]
            InheritanceList: Annotated[
                Literal["BeforeColon", "AfterColon", "BeforeComma", "AfterComma"],
                Field(
                    default="AfterColon",
                    examples=["BeforeColon", "AfterColon", "BeforeComma", "AfterComma"],
                )
            ]
            StringLiterals: Annotated[bool, Field(default=True)]
            TemplateDeclarations: Annotated[
                Literal["Leave", "No", "Multiline", "Yes"],
                Field(
                    default="Yes",
                    examples=["Leave", "No", "Multiline", "Yes"],
                )
            ]

        Break: Annotated[_Break, Field(default_factory=_Break.model_construct)]

        class _BraceWrapping(BaseModel):
            """Validate the `[tool.clang-format.BraceWrapping]` table."""
            model_config = ConfigDict(extra="forbid")
            AfterCaseLabel: Annotated[bool, Field(default=False)]
            AfterClass: Annotated[bool, Field(default=False)]
            AfterControlStatement: Annotated[Literal["Never", "Multiline", "Always"], Field(
                default="Never",
                examples=["Never", "Multiline", "Always"],
            )]
            AfterEnum: Annotated[bool, Field(default=False)]
            AfterFunction: Annotated[bool, Field(default=False)]
            AfterNamespace: Annotated[bool, Field(default=False)]
            AfterStruct: Annotated[bool, Field(default=False)]
            AfterUnion: Annotated[bool, Field(default=False)]
            AfterExternBlock: Annotated[bool, Field(default=False)]
            BeforeCatch: Annotated[bool, Field(default=False)]
            BeforeElse: Annotated[bool, Field(default=False)]
            BeforeLambdaBody: Annotated[bool, Field(default=False)]
            BeforeWhile: Annotated[bool, Field(default=False)]
            IndentBraces: Annotated[bool, Field(default=False)]
            SplitEmptyFunction: Annotated[bool, Field(default=False)]
            SplitEmptyRecord: Annotated[bool, Field(default=False)]
            SplitEmptyNamespace: Annotated[bool, Field(default=False)]

        BraceWrapping: Annotated[_BraceWrapping, Field(
            default_factory=_BraceWrapping.model_construct
        )]

        class _Indent(BaseModel):
            """Validate the `[tool.clang-format.Indent]` table."""
            model_config = ConfigDict(extra="forbid")
            CaseLabels: Annotated[bool, Field(default=True)]
            ExportBlock: Annotated[bool, Field(default=True)]
            ExternBlock: Annotated[bool, Field(default=True)]
            GotoLabels: Annotated[bool, Field(default=True)]
            PPDirectives: Annotated[Literal["None", "BeforeHash", "AfterHash", "Both"], Field(
                default="BeforeHash",
                examples=["None", "BeforeHash", "AfterHash", "Both"],
            )]
            RequiresClause: Annotated[bool, Field(default=True)]
            Width: Annotated[NonNegativeInt, Field(default=4)]
            WrappedFunctionNames: Annotated[bool, Field(default=False)]

        Indent: Annotated[_Indent, Field(
            default_factory=_Indent.model_construct
        )]

        class _IntegerLiteralSeparator(BaseModel):
            """Validate the `[tool.clang-format.IntegerLiteralSeparator]` table."""
            model_config = ConfigDict(extra="forbid")
            Binary: Annotated[int, Field(default=8, ge=-1)]
            Decimal: Annotated[int, Field(default=-1, ge=-1)]
            Hex: Annotated[int, Field(default=4, ge=-1)]

        IntegerLiteralSeparator: Annotated[_IntegerLiteralSeparator, Field(
            default_factory=_IntegerLiteralSeparator.model_construct
        )]

        class _KeepEmptyLines(BaseModel):
            """Validate the `[tool.clang-format.KeepEmptyLines]` table."""
            model_config = ConfigDict(extra="forbid")
            AtEndOfFile: Annotated[bool, Field(default=False)]
            AtStartOfBlock: Annotated[bool, Field(default=False)]
            AtStartOfFile: Annotated[bool, Field(default=False)]

        KeepEmptyLines: Annotated[_KeepEmptyLines, Field(
            default_factory=_KeepEmptyLines.model_construct
        )]

        class _NumericLiteralCase(BaseModel):
            """Validate the `[tool.clang-format.NumericLiteralCase]` table."""
            model_config = ConfigDict(extra="forbid")
            ExponentLetter: Annotated[Literal["Leave", "Upper", "Lower"], Field(
                default="Lower",
                examples=["Leave", "Upper", "Lower"],
            )]
            HexDigit: Annotated[Literal["Leave", "Upper", "Lower"], Field(
                default="Upper",
                examples=["Leave", "Upper", "Lower"],
            )]
            Prefix: Annotated[Literal["Leave", "Upper", "Lower"], Field(
                default="Lower",
                examples=["Leave", "Upper", "Lower"],
            )]
            Suffix: Annotated[Literal["Leave", "Upper", "Lower"], Field(
                default="Lower",
                examples=["Leave", "Upper", "Lower"],
            )]

        NumericLiteralCase: Annotated[_NumericLiteralCase, Field(
            default_factory=_NumericLiteralCase.model_construct
        )]

        class _SortIncludes(BaseModel):
            """Validate the `[tool.clang-format.SortIncludes]` table."""
            model_config = ConfigDict(extra="forbid")
            Enabled: Annotated[bool, Field(default=True)]
            IgnoreCase: Annotated[bool, Field(default=False)]
            IgnoreExtension: Annotated[bool, Field(default=False)]

        SortIncludes: Annotated[_SortIncludes, Field(
            default_factory=_SortIncludes.model_construct
        )]

        class _Space(BaseModel):
            """Validate the `[tool.clang-format.Space]` table."""
            model_config = ConfigDict(extra="forbid")
            AfterCStyleCast: Annotated[bool, Field(default=False)]
            AfterLogicalNot: Annotated[bool, Field(default=False)]
            AfterOperatorKeyword: Annotated[bool, Field(default=False)]
            AfterTemplateKeyword: Annotated[bool, Field(default=False)]
            BeforeAssignmentOperators: Annotated[bool, Field(default=True)]
            BeforeCaseColon: Annotated[bool, Field(default=False)]
            BeforeCpp11BracedList: Annotated[bool, Field(default=False)]
            BeforeCtorInitializerColon: Annotated[bool, Field(default=True)]
            BeforeInheritanceColon: Annotated[bool, Field(default=True)]
            BeforeJsonColon: Annotated[bool, Field(default=False)]
            BeforeRangeBasedForLoopColon: Annotated[bool, Field(default=True)]
            BeforeSquareBrackets: Annotated[bool, Field(default=False)]
            InEmptyBraces: Annotated[Literal["Never", "Block", "Always"], Field(
                default="Never",
                examples=["Never", "Block", "Always"],
            )]

            class _BeforeParensOptions(BaseModel):
                """Validate the `[tool.clang-format.Space.BeforeParensOptions]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                AfterControlStatements: Annotated[bool, Field(default=True)]
                AfterForeachMacros: Annotated[bool, Field(default=True)]
                AfterFunctionDeclarationName: Annotated[bool, Field(default=False)]
                AfterFunctionDefinitionName: Annotated[bool, Field(default=False)]
                AfterIfMacros: Annotated[bool, Field(default=True)]
                AfterNot: Annotated[bool, Field(default=True)]
                AfterOverloadedOperator: Annotated[bool, Field(default=False)]
                AfterPlacementOperator: Annotated[bool, Field(default=True)]
                AfterRequiresInClause: Annotated[bool, Field(default=False)]
                AfterRequiresInExpression: Annotated[bool, Field(default=False)]
                BeforeNonEmptyParentheses: Annotated[bool, Field(default=False)]

            BeforeParensOptions: Annotated[_BeforeParensOptions, Field(
                default_factory=_BeforeParensOptions.model_construct
            )]

            class _InParensOptions(BaseModel):
                """Validate the `[tool.clang-format.Space.InParensOptions]`
                table.
                """
                model_config = ConfigDict(extra="forbid")
                ExceptDoubleParentheses: Annotated[bool, Field(default=False)]
                InConditionalStatements: Annotated[bool, Field(default=False)]
                InCStyleCasts: Annotated[bool, Field(default=False)]
                InEmptyParentheses: Annotated[bool, Field(default=False)]
                Other: Annotated[bool, Field(default=False)]

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
            "AllowShortFunctionsOnASingleLine": model.Allow.ShortFunctionsOnASingleLine,
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
            "IndentExternBlock": model.Indent.ExternBlock,
            "IndentGotoLabels": model.Indent.GotoLabels,
            "IndentPPDirectives": model.Indent.PPDirectives,
            "IndentRequiresClause": model.Indent.RequiresClause,
            "IndentWidth": model.Indent.Width,
            "IndentWrappedFunctionNames": model.Indent.WrappedFunctionNames,
            "InsertBraces": model.InsertBraces,
            "InsertNewlineAtEOF": model.InsertNewlineAtEOF,
            "IntegerLiteralSeparator": {
                "Binary": model.IntegerLiteralSeparator.Binary,
                "Decimal": model.IntegerLiteralSeparator.Decimal,
                "Hex": model.IntegerLiteralSeparator.Hex,
            },
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

