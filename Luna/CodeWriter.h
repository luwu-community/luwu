// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace Luwu::Dsl
{

class CodeWriter
{
public:
    explicit CodeWriter(std::ostream& os, int baseIndent = 0)
        : out_(os), indentLevel_(baseIndent)
    {
    }

    virtual ~CodeWriter() = default;

    void indent()
    {
        indentLevel_++;
    }

    void dedent()
    {
        if (indentLevel_ > 0)
            indentLevel_--;
    }

    int indentLevel() const
    {
        return indentLevel_;
    }

    std::string indentStr() const
    {
        return std::string(indentLevel_ * 4, ' ');
    }

    void line(const std::string& text = "")
    {
        if (text.empty())
        {
            out_ << "\n";
        }
        else
        {
            for (int i = 0; i < indentLevel_; ++i)
                out_ << "    ";
            out_ << text << "\n";
        }
    }

    void raw(const std::string& text)
    {
        out_ << text;
    }

    template <typename Fn>
    void block(const std::string& header, Fn&& fn)
    {
        line(header);
        line("{");
        indent();
        fn();
        dedent();
        line("}");
    }

    template <typename Fn>
    void scope(Fn&& fn)
    {
        line("{");
        indent();
        fn();
        dedent();
        line("}");
    }

    void addDecl(const std::string& decl)
    {
        for (const auto& d : decls_)
        {
            if (d == decl)
                return;
        }
        decls_.push_back(decl);
    }

    const std::vector<std::string>& decls() const
    {
        return decls_;
    }

    void clearDecls()
    {
        decls_.clear();
    }

    std::ostream& stream()
    {
        return out_;
    }

private:
    std::ostream& out_;
    int indentLevel_ = 0;
    std::vector<std::string> decls_;
};

class StringCodeWriter : public CodeWriter
{
public:
    explicit StringCodeWriter(int baseIndent = 0)
        : CodeWriter(ss_, baseIndent)
    {
    }

    std::string str() const
    {
        return ss_.str();
    }

private:
    std::ostringstream ss_;
};

} // namespace Luwu::Dsl
