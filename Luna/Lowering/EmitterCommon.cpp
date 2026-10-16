// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Emitter.h"
#include "CompileError.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace Luwu::Dsl
{

std::string toCppIdent(const std::string& name)
{
    std::string result = name;
    for (char& c : result)
    {
        if (c == '-')
            c = '_';
    }
    return result;
}

std::string toUpperIdent(const std::string& name)
{
    std::string result = name;
    for (char& c : result)
    {
        if (c == '-')
            c = '_';
        else
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return result;
}

std::string toCppVarName(const std::string& name)
{
    // Names the generated lower_* functions already use for their own parameters and locals
    static const std::set<std::string> kReserved = {"lowering", "inst", "index", "next", "build", "regs", "function"};

    std::string result = name;
    for (char& c : result)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            c = '_';
    }
    if (kReserved.count(result))
        result += "_";
    return result;
}

static std::string stripLeadingColon(const std::string& s)
{
    if (!s.empty() && s[0] == ':')
        return s.substr(1);
    return s;
}

std::vector<Rule> parseRules(const std::vector<SExpr>& program)
{
    std::vector<Rule> rules;
    std::unordered_map<std::string, SourceLocation> seenNames;

    for (const auto& expr : program)
    {
        bool isDefineRule = expr.isList("define-rule");
        bool isDefineSubrule = expr.isList("define-subrule");
        if (!isDefineRule && !isDefineSubrule)
        {
            std::string what = expr.isList() && !expr.elements.empty() ? expr.elements[0].text : expr.text;
            throw CompileError(expr, "Unknown top-level form '" + what + "' (expected define-rule or define-subrule)");
        }

        std::string formName = isDefineSubrule ? "define-subrule" : "define-rule";

        if (expr.elements.size() < 3)
            throw CompileError(expr, formName + " requires a signature and at least one body expression");

        const SExpr& sig = expr.elements[1];
        if (!sig.isList() || sig.elements.empty() || !sig.elements[0].isSymbol())
            throw CompileError(sig, formName + " signature must be a non-empty list starting with the rule name");

        Rule rule;
        rule.name = sig.elements[0].text;
        rule.loc = expr.loc;
        rule.isSubrule = isDefineSubrule;

        auto [prev, inserted] = seenNames.emplace(rule.name, sig.elements[0].loc);
        if (!inserted)
            throw CompileError(sig.elements[0], "Duplicate definition of '" + rule.name + "' (previously defined at " + prev->second.toString() + ")");

        std::set<std::string> paramNames;
        for (size_t i = 1; i < sig.elements.size(); ++i)
        {
            const SExpr& p = sig.elements[i];
            if (!p.isList() || p.elements.size() != 2 || !p.elements[0].isSymbol() || !p.elements[1].isKeyword())
            {
                throw CompileError(
                    p, "Rule parameter must be a pair [name :type] in " + formName + ": " + rule.name
                );
            }

            RuleParam param;
            param.name = p.elements[0].text;
            param.type = stripLeadingColon(p.elements[1].text);
            param.loc = p.loc;

            if (!paramNames.insert(param.name).second)
                throw CompileError(p, "Duplicate parameter '" + param.name + "' in " + formName + ": " + rule.name);

            if (!rule.isSubrule && param.type == "any")
            {
                throw CompileError(
                    p, "Type :any is only allowed in define-subrule, not top-level define-rule: " + rule.name
                );
            }
            if (param.type != "reg" && param.type != "uint" && param.type != "int" &&
                param.type != "tag" && param.type != "upvalue" && param.type != "cond" &&
                param.type != "label" && param.type != "op" && param.type != "any")
            {
                throw CompileError(
                    p, "Unknown parameter type :" + param.type + " in " + formName + ": " + rule.name
                );
            }

            rule.params.push_back(std::move(param));
        }

        for (size_t i = 2; i < expr.elements.size(); ++i)
        {
            rule.body.push_back(expr.elements[i]);
        }

        rules.push_back(std::move(rule));
    }

    return rules;
}

static SExpr makeSymbol(const std::string& text, const SourceLocation& loc)
{
    SExpr sym;
    sym.kind = SExprKind::Symbol;
    sym.text = text;
    sym.loc = loc;
    return sym;
}

static SExpr makeForm(const std::string& head, const SourceLocation& loc, std::vector<SExpr> rest)
{
    SExpr form;
    form.kind = SExprKind::List;
    form.loc = loc;
    form.elements.push_back(makeSymbol(head, loc));
    for (auto& e : rest)
        form.elements.push_back(std::move(e));
    return form;
}

// Replaces symbols in operand positions; list heads (form and op names) are never substituted
static SExpr substitute(const SExpr& expr, const std::unordered_map<std::string, SExpr>& substMap)
{
    if (expr.isSymbol())
    {
        auto it = substMap.find(expr.text);
        if (it != substMap.end())
            return it->second;
        return expr;
    }

    if (expr.isList())
    {
        SExpr copy = expr;
        copy.elements.clear();
        for (size_t i = 0; i < expr.elements.size(); ++i)
        {
            const SExpr& el = expr.elements[i];
            if (i == 0 && el.isSymbol())
                copy.elements.push_back(el);
            else
                copy.elements.push_back(substitute(el, substMap));
        }
        return copy;
    }

    return expr;
}

// Like substitute, but also renames let binder positions ([name val] pairs), which substitute leaves alone
static SExpr renameSymbols(const SExpr& expr, const std::unordered_map<std::string, SExpr>& renames)
{
    if (!expr.isList())
        return substitute(expr, renames);

    SExpr copy = expr;
    bool isLet = expr.isList("let") && expr.elements.size() > 1 && expr.elements[1].isList();

    for (size_t i = 0; i < copy.elements.size(); ++i)
    {
        SExpr& el = copy.elements[i];
        if (i == 0 && el.isSymbol())
            continue;

        if (isLet && i == 1)
        {
            for (auto& binding : el.elements)
            {
                for (auto& part : binding.elements)
                    part = renameSymbols(part, renames);
            }
        }
        else
        {
            el = renameSymbols(el, renames);
        }
    }
    return copy;
}

static void collectSymbols(const SExpr& expr, std::set<std::string>& out)
{
    if (expr.isSymbol())
        out.insert(expr.text);
    for (const auto& el : expr.elements)
        collectSymbols(el, out);
}

// Names introduced by let bindings
static void collectBinders(const SExpr& expr, std::vector<const SExpr*>& out)
{
    if (!expr.isList() || expr.elements.empty())
        return;

    const std::string& head = expr.elements[0].text;
    if (head == "let" && expr.elements.size() > 1 && expr.elements[1].isList())
    {
        for (const auto& b : expr.elements[1].elements)
        {
            if (b.isList() && !b.elements.empty() && b.elements[0].isSymbol())
                out.push_back(&b.elements[0]);
        }
    }

    for (const auto& el : expr.elements)
        collectBinders(el, out);
}

class SubruleExpander
{
public:
    explicit SubruleExpander(const std::unordered_map<std::string, Rule>& ruleMap)
        : ruleMap(ruleMap)
    {
    }

    // Expands subrule calls; always yields exactly one expression so expansions are safe in any position
    SExpr expand(const SExpr& expr)
    {
        if (!expr.isList() || expr.elements.empty())
            return expr;

        const std::string& head = expr.elements[0].text;
        if (expr.elements[0].isSymbol())
        {
            auto it = ruleMap.find(head);
            if (it != ruleMap.end())
                return expandCall(expr, it->second);
        }

        SExpr copy = expr;

        // Binding pairs [var val] are not calls, even when var happens to share a subrule's name
        if (head == "let" && expr.elements.size() > 1 && expr.elements[1].isList())
        {
            for (auto& b : copy.elements[1].elements)
            {
                if (b.isList() && b.elements.size() == 2)
                    b.elements[1] = expand(b.elements[1]);
            }
            for (size_t i = 2; i < copy.elements.size(); ++i)
                copy.elements[i] = expand(copy.elements[i]);
            return copy;
        }

        for (auto& el : copy.elements)
            el = expand(el);
        return copy;
    }

private:
    std::string freshName(const std::string& base, const std::set<std::string>& avoid)
    {
        for (;;)
        {
            std::string candidate = base + "_" + std::to_string(++gensymCounter);
            if (!avoid.count(candidate))
                return candidate;
        }
    }

    SExpr expandCall(const SExpr& expr, const Rule& subrule)
    {
        const std::string& head = subrule.name;

        for (const auto& active : stack)
        {
            if (active == head)
            {
                std::string chain;
                for (const auto& s : stack)
                    chain += s + " -> ";
                throw CompileError(expr, "Recursive subrule expansion: " + chain + head);
            }
        }

        if (expr.elements.size() - 1 != subrule.params.size())
        {
            throw CompileError(expr, "Rule " + head + " expects " +
                std::to_string(subrule.params.size()) + " arguments, got " +
                std::to_string(expr.elements.size() - 1));
        }

        // Arguments are expanded in the caller's context before being substituted
        std::vector<SExpr> args;
        std::set<std::string> argSymbols;
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            args.push_back(expand(expr.elements[i]));
            collectSymbols(args.back(), argSymbols);
        }

        std::set<std::string> avoid = argSymbols;
        for (const auto& bodyExpr : subrule.body)
            collectSymbols(bodyExpr, avoid);

        std::set<std::string> paramNames;
        for (const auto& param : subrule.params)
            paramNames.insert(param.name);

        // Rename subrule-local binders that would capture symbols referenced by the caller's arguments
        std::unordered_map<std::string, SExpr> renames;
        std::vector<const SExpr*> binders;
        for (const auto& bodyExpr : subrule.body)
            collectBinders(bodyExpr, binders);

        for (const SExpr* binder : binders)
        {
            if (paramNames.count(binder->text))
                throw CompileError(*binder, "Binding '" + binder->text + "' shadows a parameter of subrule " + head);
            if (argSymbols.count(binder->text) && !renames.count(binder->text))
            {
                std::string name = freshName(binder->text, avoid);
                avoid.insert(name);
                renames[binder->text] = makeSymbol(name, binder->loc);
            }
        }

        std::vector<SExpr> bindings;
        std::unordered_map<std::string, SExpr> substMap;

        for (size_t i = 0; i < subrule.params.size(); ++i)
        {
            const auto& param = subrule.params[i];
            const SExpr& arg = args[i];

            if (param.type == "reg" && arg.isList("mem"))
            {
                throw CompileError(arg, "Cannot pass memory operand to :reg parameter '" + param.name + "' in subrule " + head);
            }

            if (param.type == "reg" && arg.isList())
            {
                // Materialize the argument once through a synthetic let binding [name arg]
                std::string name = param.name;
                if (argSymbols.count(name))
                    name = freshName(name, avoid);
                avoid.insert(name);

                SExpr binding;
                binding.kind = SExprKind::BracketList;
                binding.loc = arg.loc;
                binding.elements = {makeSymbol(name, param.loc), arg};
                bindings.push_back(binding);

                substMap[param.name] = makeSymbol(name, param.loc);
            }
            else
            {
                substMap[param.name] = arg;
            }
        }

        stack.push_back(head);

        std::vector<SExpr> expandedBody;
        for (const auto& bodyExpr : subrule.body)
        {
            SExpr renamed = renames.empty() ? bodyExpr : renameSymbols(bodyExpr, renames);
            expandedBody.push_back(expand(substitute(renamed, substMap)));
        }

        stack.pop_back();

        if (!bindings.empty())
        {
            SExpr bindingsList;
            bindingsList.kind = SExprKind::BracketList;
            bindingsList.loc = expr.loc;
            bindingsList.elements = std::move(bindings);

            std::vector<SExpr> rest;
            rest.push_back(std::move(bindingsList));
            for (auto& b : expandedBody)
                rest.push_back(std::move(b));
            return makeForm("let", expr.loc, std::move(rest));
        }

        if (expandedBody.size() == 1)
            return expandedBody[0];

        return makeForm("begin", expr.loc, std::move(expandedBody));
    }

    const std::unordered_map<std::string, Rule>& ruleMap;
    std::vector<std::string> stack;
    int gensymCounter = 0;
};

std::vector<Rule> expandRules(const std::vector<Rule>& rules)
{
    std::unordered_map<std::string, Rule> ruleMap;
    for (const auto& rule : rules)
    {
        if (rule.isSubrule)
            ruleMap[rule.name] = rule;
    }

    SubruleExpander expander(ruleMap);

    std::vector<Rule> expanded;
    for (const auto& rule : rules)
    {
        if (rule.isSubrule)
            continue; // Subrules are inlined into other rules and not emitted as top-level lowerers

        Rule newRule = rule;
        newRule.body.clear();

        for (const auto& expr : rule.body)
            newRule.body.push_back(expander.expand(expr));

        expanded.push_back(std::move(newRule));
    }

    return expanded;
}

} // namespace Luwu::Dsl
