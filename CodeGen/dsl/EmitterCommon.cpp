// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Emitter.h"
#include "CompileError.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

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

static std::string stripLeadingColon(const std::string& s)
{
    if (!s.empty() && s[0] == ':')
        return s.substr(1);
    return s;
}

std::vector<Rule> parseRules(const std::vector<SExpr>& program)
{
    std::vector<Rule> rules;

    for (const auto& expr : program)
    {
        bool isDefineRule = expr.isList("define-rule");
        bool isDefineSubrule = expr.isList("define-subrule");
        if (!isDefineRule && !isDefineSubrule)
            continue;

        std::string formName = isDefineSubrule ? "define-subrule" : "define-rule";

        if (expr.elements.size() < 3)
            throw CompileError(expr, formName + " requires a signature and at least one body expression");

        const SExpr& sig = expr.elements[1];
        if (!sig.isList() || sig.elements.empty())
            throw CompileError(sig, formName + " signature must be a non-empty list");

        Rule rule;
        rule.name = sig.elements[0].text;
        rule.loc = expr.loc;
        rule.isSubrule = isDefineSubrule;

        for (size_t i = 1; i < sig.elements.size(); ++i)
        {
            const SExpr& p = sig.elements[i];
            if (!p.isList() || p.elements.size() != 2)
            {
                throw CompileError(
                    p, "Rule parameter must be a pair [name :type], got: " + p.text
                );
            }

            RuleParam param;
            param.name = p.elements[0].text;
            param.type = stripLeadingColon(p.elements[1].text);
            param.loc = p.loc;

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
        for (const auto& el : expr.elements)
        {
            copy.elements.push_back(substitute(el, substMap));
        }
        return copy;
    }

    return expr;
}

static std::vector<SExpr> expandExpr(const SExpr& expr, const std::unordered_map<std::string, Rule>& ruleMap)
{
    if (expr.isList() && !expr.elements.empty())
    {
        const std::string& head = expr.elements[0].text;
        auto it = ruleMap.find(head);
        if (it != ruleMap.end())
        {
            const Rule& subrule = it->second;
            if (expr.elements.size() - 1 != subrule.params.size())
            {
                throw CompileError(expr, "Rule " + head + " expects " +
                    std::to_string(subrule.params.size()) + " arguments, got " +
                    std::to_string(expr.elements.size() - 1));
            }

            std::vector<SExpr> bindings;
            std::unordered_map<std::string, SExpr> substMap;

            for (size_t i = 0; i < subrule.params.size(); ++i)
            {
                const auto& param = subrule.params[i];
                const SExpr& arg = expr.elements[i + 1];

                if (param.type == "reg" && arg.isList("mem"))
                {
                    throw CompileError(arg, "Cannot pass memory operand to :reg parameter '" + param.name + "' in subrule " + head);
                }

                if (param.type == "reg" && arg.isList())
                {
                    // Create a synthetic let binding: [param.name arg]
                    SExpr binding;
                    binding.kind = SExprKind::BracketList;
                    binding.loc = arg.loc;

                    SExpr nameSym;
                    nameSym.kind = SExprKind::Symbol;
                    nameSym.text = param.name;
                    nameSym.loc = param.loc;

                    binding.elements = {nameSym, arg};
                    bindings.push_back(binding);

                    substMap[param.name] = nameSym;
                }
                else
                {
                    substMap[param.name] = arg;
                }
            }

            std::vector<SExpr> expandedBody;
            for (const auto& bodyExpr : subrule.body)
            {
                SExpr sub = substitute(bodyExpr, substMap);
                auto res = expandExpr(sub, ruleMap);
                expandedBody.insert(expandedBody.end(), res.begin(), res.end());
            }

            if (!bindings.empty())
            {
                SExpr letExpr;
                letExpr.kind = SExprKind::List;
                letExpr.loc = expr.loc;

                SExpr letSym;
                letSym.kind = SExprKind::Symbol;
                letSym.text = "let";
                letSym.loc = expr.loc;

                SExpr bindingsList;
                bindingsList.kind = SExprKind::BracketList;
                bindingsList.loc = expr.loc;
                bindingsList.elements = bindings;

                letExpr.elements.push_back(letSym);
                letExpr.elements.push_back(bindingsList);
                for (const auto& b : expandedBody)
                    letExpr.elements.push_back(b);

                return {letExpr};
            }
            else
            {
                return expandedBody;
            }
        }

        if (head == "let")
        {
            SExpr copy = expr;
            copy.elements.clear();
            copy.elements.push_back(expr.elements[0]);

            // Expand subrule expressions within let bindings: ([var val] ...)
            if (expr.elements.size() > 1)
            {
                const SExpr& bindings = expr.elements[1];
                if (bindings.isList())
                {
                    SExpr newBindings = bindings;
                    newBindings.elements.clear();
                    for (const auto& b : bindings.elements)
                    {
                        if (b.isList() && b.elements.size() == 2)
                        {
                            SExpr newB = b;
                            auto res = expandExpr(b.elements[1], ruleMap);
                            if (res.size() == 1)
                                newB.elements = {b.elements[0], res[0]};
                            newBindings.elements.push_back(newB);
                        }
                        else
                        {
                            newBindings.elements.push_back(b);
                        }
                    }
                    copy.elements.push_back(newBindings);
                }
                else
                {
                    copy.elements.push_back(bindings);
                }
            }

            for (size_t i = 2; i < expr.elements.size(); ++i)
            {
                auto res = expandExpr(expr.elements[i], ruleMap);
                copy.elements.insert(copy.elements.end(), res.begin(), res.end());
            }
            return {copy};
        }

        // For all other list expressions (cond, if, begin, target-case, %produce-*, etc.),
        // recursively expand list children so subrules can be used within any control structure.
        SExpr copy = expr;
        copy.elements.clear();
        for (const auto& el : expr.elements)
        {
            if (el.isList())
            {
                auto res = expandExpr(el, ruleMap);
                for (const auto& r : res)
                    copy.elements.push_back(r);
            }
            else
            {
                copy.elements.push_back(el);
            }
        }
        return {copy};
    }

    return {expr};
}

std::vector<Rule> expandRules(const std::vector<Rule>& rules)
{
    std::unordered_map<std::string, Rule> ruleMap;
    for (const auto& rule : rules)
    {
        if (rule.isSubrule)
            ruleMap[rule.name] = rule;
    }

    std::vector<Rule> expanded;
    for (const auto& rule : rules)
    {
        if (rule.isSubrule)
            continue; // Subrules are inlined into other rules and not emitted as top-level lowerers

        Rule newRule = rule;
        newRule.body.clear();

        for (const auto& expr : rule.body)
        {
            auto res = expandExpr(expr, ruleMap);
            newRule.body.insert(newRule.body.end(), res.begin(), res.end());
        }

        expanded.push_back(std::move(newRule));
    }

    return expanded;
}

} // namespace Luwu::Dsl
