// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Helpifier.h"

namespace Luau
{

static constexpr std::string_view kHelpStart = "Help (";
static constexpr std::string_view kTopicEnd = "):";
static constexpr std::string_view kBulletStart = "\n  • ";

std::string Helpifier::operator()(std::string_view topic, std::initializer_list<std::string_view> bullets) const
{
    return (*this)(topic, bullets.begin(), bullets.size());
}

void Helpifier::appendTo(std::string& message, std::string_view topic, std::initializer_list<std::string_view> bullets) const
{
    if (!enabled || bullets.size() == 0)
        return;

    message += "\n\n";
    message += (*this)(topic, bullets);
}

std::string Helpifier::operator()(std::string_view topic, const std::string_view* bullets, size_t count) const
{
    if (!enabled || count == 0)
        return {};

    size_t size = kHelpStart.size() + topic.size() + kTopicEnd.size();
    for (size_t i = 0; i < count; ++i)
        size += kBulletStart.size() + bullets[i].size();

    std::string help;
    help.reserve(size);
    help += kHelpStart;
    help += topic;
    help += kTopicEnd;
    for (size_t i = 0; i < count; ++i)
    {
        help += kBulletStart;
        help += bullets[i];
    }
    return help;
}

} // namespace Luau
