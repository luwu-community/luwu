// Luwu doc references: references and bindings written as Luwu code in doc comments.
#include "Luau/DocRefs.h"

#include "Luau/AstQuery.h"
#include "Luau/FileResolver.h"
#include "Luau/Module.h"
#include "Luau/ModuleResolver.h"
#include "Luau/Parser.h"
#include "Luau/Scope.h"
#include "Luau/Type.h"
#include "Luau/TypeUtils.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

LUAU_FASTFLAGVARIABLE(LuwuInLuwuDocs)

namespace Luau
{

namespace
{

bool isNameStart(char c)
{
    return isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isNameChar(char c)
{
    return isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

// A reference is a name, then `.name`s, optionally starting from `require("...")`
bool isPath(AstExpr* expr)
{
    if (expr->is<AstExprGlobal>())
        return true;
    if (AstExprIndexName* index = expr->as<AstExprIndexName>())
        return isPath(index->expr);
    if (AstExprCall* call = expr->as<AstExprCall>())
    {
        AstExprGlobal* func = call->func->as<AstExprGlobal>();
        return func && func->name == "require" && call->args.size == 1 && call->args.data[0]->is<AstExprConstantString>();
    }
    return false;
}

// A require path written on its own: `@alias`, `@alias/a/b`, `./a/b` or `../a/b`. A path naming a file with its
// extension (`./build.luau`) isn't one: require paths can't have extensions.
bool isRequirePath(std::string_view text)
{
    auto isSegmentChar = [](char c)
    {
        return isNameChar(c) || c == '-' || c == '.';
    };

    size_t at = 0;
    if (text.substr(0, 1) == "@")
    {
        at = 1;
        while (at < text.size() && text[at] != '/')
        {
            if (!isNameChar(text[at]) && text[at] != '-')
                return false;
            ++at;
        }
        if (at == 1)
            return false;
    }
    else if (text.substr(0, 2) == "./")
        at = 1;
    else if (text.substr(0, 3) == "../")
        at = 2;
    else
        return false;

    // then `/segment`, any number of times (at least once after `./` or `../`)
    size_t lastSegment = at;
    while (at < text.size())
    {
        if (text[at] != '/')
            return false;
        lastSegment = ++at;
        while (at < text.size() && isSegmentChar(text[at]))
            ++at;
        if (at == lastSegment)
            return false;
    }

    std::string_view last = text.substr(lastSegment);
    for (std::string_view extension : {".luau", ".luwu", ".lua"})
    {
        if (last.size() > extension.size() && last.substr(last.size() - extension.size()) == extension)
            return false;
    }
    return true;
}

// One line of a doc comment's text: where it starts in the source, and as a position
struct DocLine
{
    std::string_view text;
    size_t offset;
    Position position;
};

struct DocText
{
    Location location;
    std::vector<DocLine> lines;
};

class DocParser
{
public:
    DocParser(SourceModule& sourceModule, std::string_view source)
        : sourceModule(sourceModule)
        , source(source)
    {
        lineStarts.push_back(0);
        for (size_t i = 0; i < source.size(); ++i)
        {
            if (source[i] == '\n')
                lineStarts.push_back(i + 1);
        }
    }

    void run()
    {
        std::vector<DocText> texts = findDocTexts();
        if (texts.empty())
            return;
        std::unordered_map<unsigned, AstExprFunction*> functionsByLine = functionsByFirstLine();

        for (size_t i = 0; i < texts.size(); ++i)
        {
            DocComment doc;
            doc.location = texts[i].location;
            doc.moduleDoc = i == 0 && isModuleDoc(texts[i].location);

            // a comment right above a function documents it
            if (auto it = functionsByLine.find(texts[i].location.end.line + 1); it != functionsByLine.end())
                doc.functionBody = it->second->body->location.begin;

            parseText(texts[i], doc);
            if (!doc.refs.empty() || !doc.bindings.empty())
                sourceModule.docComments.push_back(std::move(doc));
        }
    }

    // `source` is all doc text, not from a file (what a type function wrote): one comment, positions relative to it
    void runOnText()
    {
        DocText text{Location{Position{0, 0}, positionOf(source.size())}, {}};
        for (size_t line = 0; line < lineStarts.size(); ++line)
        {
            size_t begin = lineStarts[line];
            size_t end = line + 1 < lineStarts.size() ? lineStarts[line + 1] - 1 : source.size();
            text.lines.push_back(DocLine{source.substr(begin, end - begin), begin, Position{unsigned(line), 0}});
        }

        DocComment doc;
        doc.location = text.location;
        parseText(text, doc);
        sourceModule.docComments.push_back(std::move(doc));
    }

private:
    SourceModule& sourceModule;
    std::string_view source;
    std::vector<size_t> lineStarts;
    // the labels the current comment defines with `[label]: url`, lowercased: a `[label]` naming one is a markdown link
    std::unordered_set<std::string> linkLabels;

    static std::string lowercase(std::string_view text)
    {
        std::string result(text);
        for (char& c : result)
            c = char(tolower(static_cast<unsigned char>(c)));
        return result;
    }

    size_t offsetOf(Position position) const
    {
        return lineStarts[position.line] + position.column;
    }

    Position positionOf(size_t offset) const
    {
        auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), offset);
        unsigned line = unsigned(it - lineStarts.begin() - 1);
        return Position{line, unsigned(offset - lineStarts[line])};
    }

    bool startsLine(Position position) const
    {
        for (size_t i = lineStarts[position.line]; i < offsetOf(position); ++i)
        {
            if (!isSpace(source[i]))
                return false;
        }
        return true;
    }

    // `---` lines (consecutive ones make one comment) and `--[=[ ]=]` blocks. `--[[ ]]` isn't a doc comment: it's how
    // code gets commented out, and `t[i]` in it would read as a reference.
    std::vector<DocText> findDocTexts() const
    {
        std::vector<DocText> texts;
        bool lastWasDocLine = false;

        for (const Comment& comment : sourceModule.commentLocations)
        {
            size_t begin = offsetOf(comment.location.begin);
            size_t end = offsetOf(comment.location.end);
            std::string_view text = source.substr(begin, end - begin);

            bool isDocLine = comment.type == Lexeme::Comment && text.substr(0, 3) == "---" && text.substr(0, 4) != "----";
            if (isDocLine && startsLine(comment.location.begin))
            {
                DocLine line{text.substr(3), begin + 3, Position{comment.location.begin.line, comment.location.begin.column + 3}};
                if (lastWasDocLine && texts.back().location.end.line + 1 == comment.location.begin.line)
                {
                    texts.back().location.end = comment.location.end;
                    texts.back().lines.push_back(line);
                }
                else
                    texts.push_back(DocText{comment.location, {line}});
                lastWasDocLine = true;
                continue;
            }

            lastWasDocLine = false;
            if (comment.type != Lexeme::BlockComment || text.substr(0, 4) != "--[=")
                continue;

            size_t open = text.find('[', 3);
            if (open == std::string_view::npos)
                continue;
            size_t level = open - 3;
            std::string closer = "]" + std::string(level, '=') + "]";
            size_t bodyBegin = open + 1;
            size_t bodyEnd = text.size();
            bool closed = text.size() >= closer.size() && text.substr(text.size() - closer.size()) == closer;
            if (closed)
                bodyEnd -= closer.size();

            DocText doc{comment.location, {}};
            size_t lineBegin = bodyBegin;
            while (lineBegin <= bodyEnd)
            {
                size_t lineEnd = text.find('\n', lineBegin);
                if (lineEnd == std::string_view::npos || lineEnd > bodyEnd)
                    lineEnd = bodyEnd;
                doc.lines.push_back(DocLine{text.substr(lineBegin, lineEnd - lineBegin), begin + lineBegin, positionOf(begin + lineBegin)});
                lineBegin = lineEnd + 1;
            }
            texts.push_back(std::move(doc));
        }

        return texts;
    }

    // the first function starting on each line
    std::unordered_map<unsigned, AstExprFunction*> functionsByFirstLine() const
    {
        struct Collector : AstVisitor
        {
            std::unordered_map<unsigned, AstExprFunction*> functions;

            bool visit(AstExprFunction* function) override
            {
                functions.emplace(function->location.begin.line, function);
                return true;
            }
        };

        Collector collector;
        if (sourceModule.root)
            sourceModule.root->visit(&collector);
        return std::move(collector.functions);
    }

    // The first doc comment is the module's if nothing comes before it and a blank line separates it from what follows
    bool isModuleDoc(const Location& location) const
    {
        if (!sourceModule.root || sourceModule.root->body.size == 0)
            return true;

        AstStat* first = sourceModule.root->body.data[0];
        return location.end < first->location.begin && first->location.begin.line > location.end.line + 1;
    }

    template<typename Node>
    ParseNodeResult<Node> parseAt(size_t offset, size_t size)
    {
        ParseOptions options;
        options.parseFragment = FragmentParseResumeSettings{DenseHashMap<AstName, AstLocal*>{AstName()}, {}, positionOf(offset)};

        const char* buffer = source.data() + offset;
        if constexpr (std::is_same_v<Node, AstType>)
            return Parser::parseType(buffer, size, *sourceModule.names, *sourceModule.allocator, options);
        else
            return Parser::parseExpr(buffer, size, *sourceModule.names, *sourceModule.allocator, options);
    }

    // `require("<path>")`, all of it at `location`
    AstExpr* requireCall(std::string_view path, const Location& location)
    {
        Allocator& allocator = *sourceModule.allocator;

        char* chars = static_cast<char*>(allocator.allocate(path.size()));
        std::copy(path.begin(), path.end(), chars);
        AstExpr* argument = allocator.alloc<AstExprConstantString>(
            location, AstArray<char>{chars, path.size()}, AstExprConstantString::QuoteStyle::QuotedSimple
        );

        AstExpr** args = static_cast<AstExpr**>(allocator.allocate(sizeof(AstExpr*)));
        args[0] = argument;
        AstExpr* require = allocator.alloc<AstExprGlobal>(location, sourceModule.names->getOrAdd("require"));
        return allocator.alloc<AstExprCall>(location, require, AstArray<AstExpr*>{args, 1}, false, AstArray<AstTypeOrPack>{nullptr, 0}, location);
    }

    void error(const Location& location, const std::string& message)
    {
        sourceModule.docParseErrors.emplace_back(location, message);
    }

    // Code that doesn't belong in a doc comment, or nullopt
    static std::optional<std::string> forbiddenCode(AstExpr* expr)
    {
        struct Finder : AstVisitor
        {
            std::optional<std::string> found;

            bool visit(AstExprFunction*) override
            {
                found = "functions can't be defined in a doc comment";
                return false;
            }
        };

        Finder finder;
        expr->visit(&finder);
        return finder.found;
    }

    // `[name = expr]`: the name, and where the expression starts in `content`
    static std::optional<std::pair<std::string_view, size_t>> matchBinding(std::string_view content)
    {
        size_t at = 0;
        while (at < content.size() && isSpace(content[at]))
            ++at;
        size_t nameBegin = at;
        if (at >= content.size() || !isNameStart(content[at]))
            return std::nullopt;
        while (at < content.size() && isNameChar(content[at]))
            ++at;
        std::string_view name = content.substr(nameBegin, at - nameBegin);
        while (at < content.size() && isSpace(content[at]))
            ++at;
        if (at >= content.size() || content[at] != '=' || (at + 1 < content.size() && content[at + 1] == '='))
            return std::nullopt;
        return std::pair{name, at + 1};
    }

    void parseBinding(std::string_view name, size_t nameOffset, size_t valueOffset, size_t valueEnd, DocComment& doc)
    {
        auto result = parseAt<AstExpr>(valueOffset, valueEnd - valueOffset);
        if (!result.errors.empty())
        {
            error(result.errors.front().getLocation(), "Invalid doc binding: " + result.errors.front().getMessage());
            return;
        }
        if (!result.root)
        {
            error(Location{positionOf(valueOffset), positionOf(valueEnd)}, "Invalid doc binding: expected an expression");
            return;
        }
        if (auto message = forbiddenCode(result.root))
        {
            error(result.root->location, *message);
            return;
        }

        Position nameBegin = positionOf(nameOffset);
        doc.bindings.push_back(DocBinding{
            sourceModule.names->getOrAdd(std::string(name).c_str()),
            Location{nameBegin, unsigned(name.size())},
            result.root
        });
    }

    void parseRef(std::string_view content, size_t offset, DocComment& doc)
    {
        // trim
        size_t begin = 0;
        while (begin < content.size() && isSpace(content[begin]))
            ++begin;
        size_t end = content.size();
        while (end > begin && isSpace(content[end - 1]))
            --end;
        if (begin == end)
            return;
        content = content.substr(begin, end - begin);
        offset += begin;
        if (linkLabels.count(lowercase(content)))
            return;

        DocRef ref;
        ref.location = Location{positionOf(offset), positionOf(offset + content.size())};

        // `[@std/str]`, `[./lexer]`: a require path stands for `require("@std/str")`
        if (isRequirePath(content))
        {
            ref.expr = requireCall(content, ref.location);
            doc.refs.push_back(ref);
            return;
        }

        if (content.substr(0, 5) == "type " || content.substr(0, 5) == "type\t")
        {
            auto result = parseAt<AstType>(offset + 5, content.size() - 5);
            if (!result.errors.empty() || !result.root || !result.root->is<AstTypeReference>())
                return;
            ref.type = result.root;
            doc.refs.push_back(ref);
            return;
        }

        // `Parser:advance`: a method of what comes before the `:`
        size_t exprSize = content.size();
        size_t colon = content.rfind(':');
        bool singleColon = colon != std::string_view::npos && colon > 0 && content[colon - 1] != ':';
        if (singleColon && colon + 1 < content.size() && isNameStart(content[colon + 1]))
        {
            bool endsWithName = true;
            for (size_t i = colon + 1; i < content.size(); ++i)
                endsWithName = endsWithName && isNameChar(content[i]);
            if (endsWithName)
            {
                exprSize = colon;
                std::string method(content.substr(colon + 1));
                ref.method = sourceModule.names->getOrAdd(method.c_str());
                ref.methodLocation = Location{positionOf(offset + colon + 1), positionOf(offset + content.size())};
            }
        }

        auto result = parseAt<AstExpr>(offset, exprSize);
        // anything that doesn't parse is prose: `[optional, default 0]`
        if (!result.errors.empty() || !result.root)
            return;

        if (auto message = forbiddenCode(result.root))
        {
            error(result.root->location, *message);
            return;
        }
        // only a path is a reference; `[1]` in `t[1]` and `[i + 1]` are prose
        if (!isPath(result.root))
            return;

        ref.expr = result.root;
        doc.refs.push_back(ref);
    }

    // `label` in a `[label]: url` line, a markdown link definition
    static std::optional<std::string_view> linkDefinitionLabel(std::string_view line)
    {
        size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string_view::npos || line[first] != '[')
            return std::nullopt;
        size_t close = matchBracket(line, first);
        if (close == std::string_view::npos || close + 1 >= line.size() || line[close + 1] != ':')
            return std::nullopt;
        return line.substr(first + 1, close - first - 1);
    }

    // The matching `]` for the `[` at `open`, skipping nested brackets and strings, or npos
    static size_t matchBracket(std::string_view line, size_t open)
    {
        int depth = 0;
        for (size_t i = open; i < line.size(); ++i)
        {
            char c = line[i];
            if (c == '"' || c == '\'')
            {
                for (++i; i < line.size() && line[i] != c; ++i)
                {
                    if (line[i] == '\\')
                        ++i;
                }
                if (i >= line.size())
                    return std::string_view::npos;
            }
            else if (c == '[')
                ++depth;
            else if (c == ']' && --depth == 0)
                return i;
        }
        return std::string_view::npos;
    }

    void parseLine(const DocLine& line, DocComment& doc)
    {
        std::string_view text = line.text;

        size_t first = 0;
        while (first < text.size() && isSpace(text[first]))
            ++first;
        size_t last = text.size();
        while (last > first && isSpace(text[last - 1]))
            --last;

        // `[name = expr]` alone on its line
        if (first < last && text[first] == '[' && matchBracket(text, first) == last - 1)
        {
            std::string_view content = text.substr(first + 1, last - first - 2);
            if (auto binding = matchBinding(content))
            {
                size_t contentOffset = line.offset + first + 1;
                size_t nameOffset = contentOffset + (binding->first.data() - content.data());
                parseBinding(binding->first, nameOffset, contentOffset + binding->second, line.offset + last - 1, doc);
                return;
            }
        }

        if (linkDefinitionLabel(text))
            return;

        // `- [ ]` / `- [x]`, a task list item's checkbox
        size_t skipCheckbox = std::string_view::npos;
        if (first + 1 < last && (text[first] == '-' || text[first] == '*' || text[first] == '+') && isSpace(text[first + 1]))
        {
            size_t box = first + 1;
            while (box < last && isSpace(text[box]))
                ++box;
            bool isBox = box + 2 < last && text[box] == '[' && text[box + 2] == ']';
            if (isBox && (text[box + 1] == ' ' || text[box + 1] == 'x' || text[box + 1] == 'X'))
                skipCheckbox = box;
        }

        for (size_t i = 0; i < text.size(); ++i)
        {
            char c = text[i];
            if (c == '\\')
            {
                ++i;
                continue;
            }
            if (c == '`')
            {
                // a code span: skip to the backtick run that closes it
                size_t runEnd = text.find_first_not_of('`', i);
                if (runEnd == std::string_view::npos)
                    break;
                std::string_view run = text.substr(i, runEnd - i);
                size_t close = text.find(run, runEnd);
                if (close == std::string_view::npos)
                    break;
                i = close + run.size() - 1;
                continue;
            }
            if (c != '[' || i == skipCheckbox || (i > 0 && text[i - 1] == '!'))
                continue;

            size_t close = matchBracket(text, i);
            if (close == std::string_view::npos)
                continue;

            size_t next = close + 1;
            if (next < text.size() && text[next] == '(')
            {
                // `[text](url)`
                size_t paren = text.find(')', next);
                i = paren == std::string_view::npos ? text.size() : paren;
                continue;
            }

            size_t contentBegin = i + 1;
            size_t contentEnd = close;
            if (next < text.size() && text[next] == '[')
            {
                // `[text][target]`; `[target][]` is the same as `[target]`
                size_t secondClose = matchBracket(text, next);
                if (secondClose == std::string_view::npos)
                    continue;
                if (secondClose > next + 1)
                {
                    contentBegin = next + 1;
                    contentEnd = secondClose;
                }
                close = secondClose;
            }

            parseRef(text.substr(contentBegin, contentEnd - contentBegin), line.offset + contentBegin, doc);
            i = close;
        }
    }

    void parseText(const DocText& text, DocComment& doc)
    {
        linkLabels.clear();
        for (const DocLine& line : text.lines)
        {
            if (auto label = linkDefinitionLabel(line.text))
                linkLabels.insert(lowercase(*label));
        }

        bool inFence = false;
        for (const DocLine& line : text.lines)
        {
            size_t first = 0;
            while (first < line.text.size() && isSpace(line.text[first]))
                ++first;
            std::string_view rest = line.text.substr(first);
            if (rest.substr(0, 3) == "```" || rest.substr(0, 3) == "~~~")
            {
                inFence = !inFence;
                continue;
            }
            if (!inFence)
                parseLine(line, doc);
        }
    }
};

class DocChecker
{
public:
    // Errors go to `errors` (none when null) and where names resolved to `targets`
    DocChecker(
        const SourceModule& sourceModule,
        const Module& module,
        ErrorVec* errors,
        std::vector<DocRefTarget>& targets,
        NotNull<BuiltinTypes> builtinTypes,
        NotNull<FileResolver> fileResolver,
        NotNull<ModuleResolver> moduleResolver,
        const TypeCheckLimits& limits
    )
        : sourceModule(sourceModule)
        , module(module)
        , errors(errors)
        , targets(targets)
        , builtinTypes(builtinTypes)
        , fileResolver(fileResolver)
        , moduleResolver(moduleResolver)
        , limits(limits)
    {
    }

    void run()
    {
        for (const ParseError& error : sourceModule.docParseErrors)
            reportDoc(error.getLocation(), DocSyntaxError{error.getMessage()});

        for (const DocComment& doc : sourceModule.docComments)
        {
            ScopePtr scope = scopeFor(doc);
            if (doc.moduleDoc)
            {
                checkRefs(doc, moduleDocBindings(), scope);
                continue;
            }

            Bindings own;
            bind(doc, own, own, scope);
            checkRefs(doc, own, scope);
        }
    }

    // A comment that isn't in the source (`text` holds its references): resolved in the scope at `at`, with every error
    // reported there
    void runOnText(const SourceModule& text, const Location& at)
    {
        reportAt = at;
        for (const ParseError& error : text.docParseErrors)
            reportDoc(error.getLocation(), DocSyntaxError{error.getMessage()});

        ScopePtr scope = findScopeAtPosition(module, at.begin);
        if (!scope)
            scope = module.getModuleScope();
        for (const DocComment& doc : text.docComments)
        {
            Bindings own;
            bind(doc, own, own, scope);
            checkRefs(doc, own, scope);
        }
    }

private:
    struct Definition
    {
        ModuleName module;
        Location location;
    };

    // What a reference or binding resolved to. `ty` is nullopt when nothing is known about it (an expression doc refs
    // don't check, a module that hasn't been checked); `definition` is where it's declared, when that's known.
    struct Resolved
    {
        std::optional<TypeId> ty;
        std::optional<Definition> definition;
    };

    using Bindings = std::unordered_map<std::string, Resolved>;

    const SourceModule& sourceModule;
    const Module& module;
    ErrorVec* errors;
    std::vector<DocRefTarget>& targets;
    NotNull<BuiltinTypes> builtinTypes;
    NotNull<FileResolver> fileResolver;
    NotNull<ModuleResolver> moduleResolver;
    const TypeCheckLimits& limits;
    Bindings moduleBindings;
    bool moduleBindingsBound = false;
    // set for text that isn't in the source: every error goes here
    std::optional<Location> reportAt;

public:
    // a reference that doesn't resolve is a type error only in strict mode; the comments' syntax errors always are
    bool reportReferences = true;

private:
    // The module-level doc's bindings, which every comment sees. Evaluated on first use: most comments never look one up,
    // and editors resolve a comment's references for every completion item that shows it.
    const Bindings& moduleDocBindings()
    {
        if (moduleBindingsBound)
            return moduleBindings;
        // set first, so a binding that refers to another sees the ones bound so far rather than starting over
        moduleBindingsBound = true;
        for (const DocComment& doc : sourceModule.docComments)
        {
            if (doc.moduleDoc)
                bind(doc, moduleBindings, moduleBindings, scopeFor(doc));
        }
        return moduleBindings;
    }

    void reportDoc(const Location& location, TypeErrorData data)
    {
        if (!errors)
            return;
        // Text a type function wrote often copies a source comment (`out:metadata().comments`), whose own check already
        // reported this where it's written: the copy's error would only repeat it, squiggling the whole call
        if (reportAt)
        {
            for (const TypeError& existing : *errors)
            {
                if (existing.inDocComment && existing.location != *reportAt && existing.data == data)
                    return;
            }
        }
        TypeError& reported = errors->emplace_back(reportAt.value_or(location), sourceModule.name, std::move(data));
        reported.inDocComment = true;
    }

    void report(const Location& location, TypeErrorData data)
    {
        if (reportReferences)
            reportDoc(location, std::move(data));
    }

    Resolved resolved(const Location& location, std::optional<TypeId> ty, std::optional<Definition> definition)
    {
        if (definition)
            targets.push_back(DocRefTarget{location, definition->module, definition->location});
        return Resolved{ty, std::move(definition)};
    }

    // Where a value of type `ty` was declared, for a name whose binding doesn't say (a global)
    std::optional<Definition> definitionOf(std::optional<TypeId> ty) const
    {
        if (!ty)
            return std::nullopt;
        TypeId followed = follow(*ty);
        if (const FunctionType* function = get<FunctionType>(followed); function && function->definition)
        {
            if (function->definition->definitionModuleName)
                return Definition{*function->definition->definitionModuleName, function->definition->definitionLocation};
        }
        if (const TableType* table = get<TableType>(followed); table && !table->definitionModuleName.empty())
            return Definition{table->definitionModuleName, table->definitionLocation};
        if (const ExternType* extern_ = get<ExternType>(followed); extern_ && extern_->definitionLocation)
        {
            if (!extern_->definitionModuleName.empty())
                return Definition{extern_->definitionModuleName, *extern_->definitionLocation};
        }
        return std::nullopt;
    }

    ScopePtr scopeFor(const DocComment& doc) const
    {
        if (doc.functionBody)
        {
            if (ScopePtr scope = findScopeAtPosition(module, *doc.functionBody))
                return scope;
        }
        if (ScopePtr scope = findScopeAtPosition(module, doc.location.begin))
            return scope;
        return module.getModuleScope();
    }

    // Where a type alias seen from `scope` is declared: in this module, or in a definitions file (`@seal`)
    std::optional<Definition> typeAliasDefinition(const ScopePtr& scope, const std::string& name) const
    {
        ScopePtr moduleScope = module.getModuleScope();
        bool inModule = true;
        for (const Scope* s = scope.get(); s; s = s->parent.get())
        {
            if (inModule)
            {
                if (auto it = s->typeAliasLocations.find(name); it != s->typeAliasLocations.end())
                    return Definition{sourceModule.name, it->second};
            }
            if (auto it = s->definitionFileTypeModules.find(name); it != s->definitionFileTypeModules.end())
            {
                auto type = s->exportedTypeBindings.find(name);
                if (type != s->exportedTypeBindings.end() && type->second.definitionLocation)
                    return Definition{it->second, *type->second.definitionLocation};
                return std::nullopt;
            }
            if (s == moduleScope.get())
                inModule = false;
        }
        return std::nullopt;
    }

    void bind(const DocComment& doc, Bindings& into, const Bindings& visible, const ScopePtr& scope)
    {
        for (const DocBinding& binding : doc.bindings)
        {
            Resolved value;
            if (isPath(binding.value))
                value = evaluate(binding.value, visible, scope);
            into[binding.name.value] = value;
        }
    }

    void checkRefs(const DocComment& doc, const Bindings& own, const ScopePtr& scope)
    {
        for (const DocRef& ref : doc.refs)
        {
            if (ref.type)
            {
                checkTypeRef(ref.type->as<AstTypeReference>(), scope);
                continue;
            }

            Resolved value = evaluate(ref.expr, own, scope);
            if (ref.method && value.ty)
                member(*value.ty, ref.method->value, ref.methodLocation);
        }
    }

    void checkTypeRef(AstTypeReference* ref, const ScopePtr& scope)
    {
        if (!ref)
            return;
        std::string name = ref->name.value;
        if (ref->prefix)
        {
            if (!scope->lookupImportedType(ref->prefix->value, name))
                report(ref->nameLocation, UnknownSymbol{std::string(ref->prefix->value) + "." + name, UnknownSymbol::Type});
            return;
        }

        if (!scope->lookupType(name))
            report(ref->nameLocation, UnknownSymbol{name, UnknownSymbol::Type});
        else if (std::optional<Definition> definition = typeAliasDefinition(scope, name))
            resolved(ref->nameLocation, std::nullopt, definition);
    }

    Resolved lookupName(const AstExprGlobal* global, const Bindings& own, const ScopePtr& scope)
    {
        std::string name = global->name.value;
        if (auto it = own.find(name); it != own.end())
            return resolved(global->location, it->second.ty, it->second.definition);
        const Bindings& moduleDoc = moduleDocBindings();
        if (auto it = moduleDoc.find(name); it != moduleDoc.end())
            return resolved(global->location, it->second.ty, it->second.definition);
        if (auto binding = scope->linearSearchForBindingPair(name, true))
        {
            std::optional<TypeId> ty = follow(binding->second.typeId);
            // a local's binding says where it is; a global's comes from elsewhere, so its type has to
            if (binding->first.local)
                return resolved(global->location, ty, Definition{sourceModule.name, binding->second.location});
            return resolved(global->location, ty, definitionOf(ty));
        }
        // a type name is a reference too: `[Pathlike]`
        if (scope->lookupType(name))
        {
            return resolved(global->location, std::nullopt, typeAliasDefinition(scope, name));
        }

        report(global->location, UnknownSymbol{name, UnknownSymbol::Binding});
        return {};
    }

    Resolved requireType(AstExprCall* call)
    {
        AstExprConstantString* pathExpr = call->args.data[0]->as<AstExprConstantString>();
        ModuleInfo context{sourceModule.name};
        std::optional<ModuleInfo> info = fileResolver->resolveModule(&context, pathExpr, limits);
        // whether the module exists only matters when it's reported, and finding out reads the file
        bool missing = !info || (errors && reportReferences && !fileResolver->readSource(info->name));
        if (missing)
        {
            report(pathExpr->location, UnknownRequire{std::string(pathExpr->value.data, pathExpr->value.size)});
            return {};
        }

        // A doc comment's require isn't a dependency of the module (docs that mention each other would make cycles), so
        // the module may not have been checked yet; then nothing is known about it.
        std::optional<TypeId> ty;
        ModulePtr required = moduleResolver->getModule(info->name);
        if (required && required->returnType)
            ty = first(required->returnType);
        // the whole call is the name here: `[require("./x")]` points where it ends, after the `)`
        return resolved(call->location, ty, Definition{info->name, Location{}});
    }

    Resolved evaluate(AstExpr* expr, const Bindings& own, const ScopePtr& scope)
    {
        if (AstExprGlobal* global = expr->as<AstExprGlobal>())
            return lookupName(global, own, scope);
        if (AstExprCall* call = expr->as<AstExprCall>())
            return requireType(call);
        if (AstExprIndexName* index = expr->as<AstExprIndexName>())
        {
            Resolved base = evaluate(index->expr, own, scope);
            if (!base.ty)
                return {};
            return member(*base.ty, index->index.value, index->indexLocation);
        }
        return {};
    }

    Resolved memberOf(const Property& prop, const ModuleName& owner, const Location& location)
    {
        std::optional<TypeId> ty = prop.readTy ? std::optional<TypeId>{follow(*prop.readTy)} : std::nullopt;
        if (prop.location)
            return resolved(location, ty, Definition{owner.empty() ? sourceModule.name : owner, *prop.location});
        return resolved(location, ty, definitionOf(ty));
    }

    // `name` in `ty`; reports it missing only where the members are known exactly
    Resolved member(TypeId ty, const std::string& name, const Location& location)
    {
        ty = follow(ty);
        if (const TableType* table = get<TableType>(ty))
        {
            if (auto it = table->props.find(name); it != table->props.end())
                return memberOf(it->second, table->definitionModuleName, location);
            bool membersOpen = table->indexer || table->state == TableState::Free || table->state == TableState::Generic;
            if (membersOpen)
                return {};
            report(location, UnknownProperty{ty, name});
            return {};
        }
        if (const ExternType* extern_ = get<ExternType>(ty))
        {
            if (const Property* prop = lookupExternTypeProp(extern_, name))
                return memberOf(*prop, extern_->definitionModuleName, location);
            if (extern_->indexer)
                return {};
            report(location, UnknownProperty{ty, name});
            return {};
        }
        if (get<MetatableType>(ty))
        {
            ErrorVec ignored;
            std::optional<TypeId> found = findTablePropertyRespectingMeta(builtinTypes, ignored, ty, name, ValueContext::RValue, location, true);
            return resolved(location, found, definitionOf(found));
        }
        return {};
    }
};

} // namespace

void parseDocComments(SourceModule& sourceModule, std::string_view source)
{
    DocParser parser{sourceModule, source};
    parser.run();
}

std::vector<std::string> metadataDocLines(const TypeMetadataTable& metadata)
{
    auto stringField = [](const TypeMetadataTable& table, const char* name) -> const std::string*
    {
        for (const auto& [key, value] : table.entries)
        {
            const std::string* keyName = get_if<std::string>(&key);
            if (keyName && *keyName == name)
                return get_if<std::string>(&value);
        }
        return nullptr;
    };

    // `comments` is a list; a type function's table can come back in any order, so go by index
    std::vector<std::pair<double, const TypeMetadataTable*>> entries;
    for (const auto& [key, value] : metadata.entries)
    {
        const std::string* keyName = get_if<std::string>(&key);
        const auto* list = get_if<std::shared_ptr<const TypeMetadataTable>>(&value);
        if (!keyName || *keyName != "comments" || !list || !*list)
            continue;

        for (const auto& [index, entry] : (*list)->entries)
        {
            const double* position = get_if<double>(&index);
            const auto* comment = get_if<std::shared_ptr<const TypeMetadataTable>>(&entry);
            if (position && comment && *comment)
                entries.emplace_back(*position, comment->get());
        }
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const auto& a, const auto& b)
        {
            return a.first < b.first;
        }
    );

    std::vector<std::string> lines;
    for (const auto& [_, comment] : entries)
    {
        const std::string* prefix = stringField(*comment, "prefix");
        const std::string* message = stringField(*comment, "message");
        if (!prefix || !message)
            continue;

        if (*prefix == "---")
        {
            lines.push_back(*message);
            continue;
        }
        if (prefix->substr(0, 3) != "--[")
            continue;

        // a block comment's lines, with their common indentation trimmed (empty lines don't count)
        std::vector<std::string> block;
        for (std::string_view line : split(*message, '\n'))
        {
            size_t end = line.find_last_not_of(" \t\r");
            block.emplace_back(end == std::string_view::npos ? std::string_view{} : line.substr(0, end + 1));
        }

        size_t indent = std::string::npos;
        for (const std::string& line : block)
        {
            if (!line.empty())
                indent = std::min(indent, line.find_first_not_of(" \t"));
        }
        for (std::string& line : block)
        {
            if (!line.empty() && indent != std::string::npos)
                line.erase(0, indent);
            lines.push_back(std::move(line));
        }
    }

    return lines;
}

const TypeMetadataTable* docMetadataOf(TypeId ty)
{
    // bound types don't form cycles; follow() relies on that too
    while (true)
    {
        if (ty->metadata)
            return ty->metadata.get();
        const BoundType* bound = get<BoundType>(ty);
        if (!bound)
            return nullptr;
        ty = bound->boundTo;
    }
}

std::vector<DocTextLink> resolveDocText(
    std::string_view text,
    const SourceModule& sourceModule,
    const Module& module,
    const Location& at,
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<FileResolver> fileResolver,
    NotNull<ModuleResolver> moduleResolver,
    const TypeCheckLimits& limits,
    std::vector<TypeError>* errors
)
{
    SourceModule parsed;
    parsed.name = sourceModule.name;
    DocParser parser{parsed, text};
    parser.runOnText();

    std::vector<DocRefTarget> targets;
    DocChecker checker{sourceModule, module, errors, targets, builtinTypes, fileResolver, moduleResolver, limits};
    checker.runOnText(parsed, at);

    std::vector<size_t> lineStarts{0};
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\n')
            lineStarts.push_back(i + 1);
    }
    auto offsetOf = [&](Position position)
    {
        return lineStarts[position.line] + position.column;
    };

    std::vector<DocTextLink> links;
    for (const DocComment& doc : parsed.docComments)
    {
        for (const DocRef& ref : doc.refs)
        {
            // the reference points where its last name does
            Position end = ref.methodLocation.end;
            if (!ref.method)
                end = ref.expr ? ref.expr->location.end : ref.type->location.end;

            std::optional<DocRefTarget> target;
            for (const DocRefTarget& candidate : targets)
            {
                if (candidate.location.end == end)
                    target = candidate;
            }

            size_t open = text.rfind('[', offsetOf(ref.location.begin));
            size_t close = text.find(']', offsetOf(ref.location.end));
            if (open == std::string_view::npos || close == std::string_view::npos)
                continue;
            links.push_back(DocTextLink{open, close, open > 0 && text[open - 1] == ']', target});
        }
    }

    return links;
}

// Where to report errors in doc text a type function wrote at `call`: the type function's name, not the whole call, which
// can span every line of the type it's given
static Location docTextErrorLocation(const SourceModule& sourceModule, const Location& call)
{
    struct Finder : AstVisitor
    {
        Location call;
        std::optional<Location> name;

        bool visit(AstTypeReference* ref) override
        {
            if (ref->location == call)
                name = ref->nameLocation;
            return !name && ref->location.encloses(call);
        }
        bool visit(AstType* type) override
        {
            return !name && type->location.encloses(call);
        }
        bool visit(AstNode* node) override
        {
            return !name && node->location.encloses(call);
        }
    };

    Finder finder;
    finder.call = call;
    if (sourceModule.root)
        sourceModule.root->visit(&finder);
    return finder.name.value_or(call);
}

void checkDocComments(
    const SourceModule& sourceModule,
    Module& module,
    NotNull<BuiltinTypes> builtinTypes,
    NotNull<FileResolver> fileResolver,
    NotNull<ModuleResolver> moduleResolver,
    const TypeCheckLimits& limits,
    Mode mode
)
{
    if (mode == Mode::NoCheck)
        return;
    DocChecker checker{sourceModule, module, &module.errors, module.docRefTargets, builtinTypes, fileResolver, moduleResolver, limits};
    checker.reportReferences = mode == Mode::Strict;
    checker.run();

    // Doc comments a type function wrote in this module are checked at the call that wrote them. Each set of comments
    // once, however many types share it.
    std::unordered_set<const TypeMetadataTable*> seen;
    auto check = [&](const TypeMetadataTable* metadata)
    {
        bool writtenHere = metadata && metadata->docContext && metadata->docContext->generated && metadata->docContext->module == sourceModule.name;
        if (!writtenHere || !seen.insert(metadata).second)
            return;

        std::string text;
        for (const std::string& line : metadataDocLines(*metadata))
            text += line + "\n";

        // like a source comment's references, these are type errors in strict mode only
        ErrorVec* errors = mode == Mode::Strict ? &module.errors : nullptr;
        Location at = docTextErrorLocation(sourceModule, metadata->docContext->location);
        resolveDocText(text, sourceModule, module, at, builtinTypes, fileResolver, moduleResolver, limits, errors);
    };

    for (const auto& [_, ty] : module.astResolvedTypes)
    {
        check(docMetadataOf(ty));

        // and the comments of its properties (`setpropertycomments`)
        if (const TableType* table = get<TableType>(follow(ty)))
        {
            for (const auto& [_, prop] : table->props)
                check(prop.metadata.get());
        }
    }
}

} // namespace Luau
