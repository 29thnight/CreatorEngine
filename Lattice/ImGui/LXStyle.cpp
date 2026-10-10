#include "LXStyle.h"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#include <Windows.h>

namespace LX
{
namespace
{
constexpr std::size_t kStyleEntryLimit = 100000;
std::atomic_uint64_t gSaveSequence = 0;
std::filesystem::path Utf8Path(const std::string& value)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

bool Fail(std::string* error, const char* message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

void WriteColor(std::ostream& out, const ImVec4& color)
{
    out << color.x << ' ' << color.y << ' ' << color.z << ' ' << color.w << ' ';
}

bool ReadColor(std::istream& in, ImVec4& color)
{
    return static_cast<bool>(in >> color.x >> color.y >> color.z >> color.w) && std::isfinite(color.x) &&
           std::isfinite(color.y) && std::isfinite(color.z) && std::isfinite(color.w);
}

void WriteStyle(std::ostream& out, const LXCanvasStyle& style)
{
    WriteColor(out, style.background);
    WriteColor(out, style.grid);
    WriteColor(out, style.shadow);
    out << style.gridSpacing << ' ' << style.shadowOffset << ' ' << style.minZoom << ' ' << style.maxZoom << ' '
        << style.zoomStep << ' ' << style.showGrid << ' ' << static_cast<int>(style.gridPattern) << ' '
        << style.gridDotRadius;
}

bool ReadStyle(std::istream& in, LXCanvasStyle& style, int version)
{
    int showGrid = 0;
    if (!ReadColor(in, style.background) || !ReadColor(in, style.grid) || !ReadColor(in, style.shadow) ||
        !(in >> style.gridSpacing >> style.shadowOffset >> style.minZoom >> style.maxZoom >> style.zoomStep >>
          showGrid))
    {
        return false;
    }
    style.showGrid = showGrid != 0;
    if (version >= 6)
    {
        int pattern = 0;
        if (!(in >> pattern >> style.gridDotRadius) || pattern < 0 || pattern > 1 ||
            !std::isfinite(style.gridDotRadius) || style.gridDotRadius <= 0.0f)
        {
            return false;
        }
        style.gridPattern = static_cast<LXGridPattern>(pattern);
    }
    return std::isfinite(style.gridSpacing) && style.gridSpacing > 0.0f && std::isfinite(style.shadowOffset) &&
           std::isfinite(style.minZoom) && style.minZoom > 0.0f && std::isfinite(style.maxZoom) &&
           style.maxZoom >= style.minZoom && std::isfinite(style.zoomStep) && style.zoomStep > 1.0f &&
           (showGrid == 0 || showGrid == 1);
}

void WriteStyle(std::ostream& out, const LXFrameStyle& style)
{
    WriteColor(out, style.fill);
    WriteColor(out, style.header);
    WriteColor(out, style.border);
    WriteColor(out, style.selectedBorder);
    WriteColor(out, style.text);
    out << style.headerHeight << ' ' << style.rounding << ' ' << style.borderThickness << ' '
        << style.selectedBorderThickness;
}

bool ReadStyle(std::istream& in, LXFrameStyle& style)
{
    return ReadColor(in, style.fill) && ReadColor(in, style.header) && ReadColor(in, style.border) &&
           ReadColor(in, style.selectedBorder) && ReadColor(in, style.text) &&
           static_cast<bool>(in >> style.headerHeight >> style.rounding >> style.borderThickness >>
                             style.selectedBorderThickness) &&
           std::isfinite(style.headerHeight) && style.headerHeight > 0.0f && std::isfinite(style.rounding) &&
           style.rounding >= 0.0f && std::isfinite(style.borderThickness) && style.borderThickness > 0.0f &&
           std::isfinite(style.selectedBorderThickness) && style.selectedBorderThickness > 0.0f;
}

void WriteStyle(std::ostream& out, const LXNodeStyle& style)
{
    WriteColor(out, style.fill);
    WriteColor(out, style.header);
    WriteColor(out, style.headerBottom);
    WriteColor(out, style.border);
    WriteColor(out, style.selectedBorder);
    WriteColor(out, style.separator);
    WriteColor(out, style.text);
    WriteColor(out, style.propertyFill);
    out << style.width << ' ' << style.height << ' ' << style.headerHeight << ' ' << style.rowHeight << ' '
        << style.bodyBottomPadding << ' ' << style.propertyPreviewHeight << ' ' << style.propertyMargin << ' '
        << style.rounding << ' ' << style.borderThickness << ' ' << style.selectedBorderThickness << ' '
        << style.fontSize << ' ' << style.textPaddingX << ' ' << style.headerTextOffsetY << ' '
        << style.showPropertyPreview << ' ' << static_cast<int>(style.pinLayout) << ' ' << style.headerOnly;
}

bool ReadStyle(std::istream& in, LXNodeStyle& style, int version)
{
    int showPropertyPreview = 0;
    if (!ReadColor(in, style.fill) || !ReadColor(in, style.header) || !ReadColor(in, style.headerBottom) ||
        !ReadColor(in, style.border) || !ReadColor(in, style.selectedBorder) || !ReadColor(in, style.separator) ||
        !ReadColor(in, style.text) || !ReadColor(in, style.propertyFill) ||
        !(in >> style.width >> style.height >> style.headerHeight >> style.rowHeight >> style.bodyBottomPadding >>
          style.propertyPreviewHeight >> style.propertyMargin >> style.rounding >> style.borderThickness >>
          style.selectedBorderThickness >> style.fontSize >> style.textPaddingX >> style.headerTextOffsetY >>
          showPropertyPreview))
    {
        return false;
    }
    int pinLayout = 0;
    if (version >= 2 && !(in >> pinLayout))
    {
        return false;
    }
    int headerOnly = 0;
    if (version >= 5 && !(in >> headerOnly))
    {
        return false;
    }
    style.pinLayout = static_cast<LXPinLayout>(pinLayout);
    style.showPropertyPreview = showPropertyPreview != 0;
    style.headerOnly = headerOnly != 0;
    return std::isfinite(style.width) && style.width > 0.0f && std::isfinite(style.height) && style.height >= 0.0f &&
           std::isfinite(style.headerHeight) && style.headerHeight > 0.0f && std::isfinite(style.rowHeight) &&
           style.rowHeight > 0.0f && std::isfinite(style.bodyBottomPadding) && style.bodyBottomPadding >= 0.0f &&
           std::isfinite(style.propertyPreviewHeight) && style.propertyPreviewHeight >= 0.0f &&
           std::isfinite(style.propertyMargin) && style.propertyMargin >= 0.0f && std::isfinite(style.rounding) &&
           style.rounding >= 0.0f && std::isfinite(style.borderThickness) && style.borderThickness > 0.0f &&
           std::isfinite(style.selectedBorderThickness) && style.selectedBorderThickness > 0.0f &&
           std::isfinite(style.fontSize) && style.fontSize > 0.0f && std::isfinite(style.textPaddingX) &&
           style.textPaddingX >= 0.0f && std::isfinite(style.headerTextOffsetY) &&
           (showPropertyPreview == 0 || showPropertyPreview == 1) && pinLayout >= 0 && pinLayout <= 1 &&
           (headerOnly == 0 || headerOnly == 1);
}

void WriteStyle(std::ostream& out, const LXItemStyle& style)
{
    WriteColor(out, style.fill);
    WriteColor(out, style.border);
    WriteColor(out, style.title);
    WriteColor(out, style.detail);
    out << style.rounding;
}

bool ReadStyle(std::istream& in, LXItemStyle& style)
{
    return ReadColor(in, style.fill) && ReadColor(in, style.border) && ReadColor(in, style.title) &&
           ReadColor(in, style.detail) && static_cast<bool>(in >> style.rounding) && std::isfinite(style.rounding) &&
           style.rounding >= 0.0f;
}

void WriteStyle(std::ostream& out, const LXPinStyle& style)
{
    out << static_cast<int>(style.shape) << ' ';
    WriteColor(out, style.fill);
    WriteColor(out, style.border);
    out << style.radius << ' ' << style.hitPadding << ' ' << style.borderThickness << ' ' << style.visible;
}

bool ReadStyle(std::istream& in, LXPinStyle& style, int version)
{
    int shape = 0;
    if (!(in >> shape) || !ReadColor(in, style.fill) || !ReadColor(in, style.border) ||
        !(in >> style.radius >> style.hitPadding >> style.borderThickness))
    {
        return false;
    }
    int visible = 1;
    if (version >= 5 && !(in >> visible))
    {
        return false;
    }
    style.shape = static_cast<LXPinShape>(shape);
    style.visible = visible != 0;
    return shape >= 0 && shape <= static_cast<int>(LXPinShape::Diamond) && std::isfinite(style.radius) &&
           style.radius > 0.0f && std::isfinite(style.hitPadding) && style.hitPadding >= 0.0f &&
           std::isfinite(style.borderThickness) && style.borderThickness > 0.0f && (visible == 0 || visible == 1);
}

void WriteStyle(std::ostream& out, const LXWireStyle& style)
{
    WriteColor(out, style.color);
    WriteColor(out, style.selectedColor);
    out << style.thickness << ' ' << style.selectedThickness << ' ' << style.bend << ' '
        << static_cast<int>(style.route) << ' ' << style.arrowAtMidpoint;
}

bool ReadStyle(std::istream& in, LXWireStyle& style, int version)
{
    if (!ReadColor(in, style.color) || !ReadColor(in, style.selectedColor) ||
        !(in >> style.thickness >> style.selectedThickness >> style.bend))
    {
        return false;
    }
    int route = 0;
    if (version >= 3 && !(in >> route))
    {
        return false;
    }
    int arrowAtMidpoint = 0;
    if (version >= 5 && !(in >> arrowAtMidpoint))
    {
        return false;
    }
    style.route = static_cast<LXWireStyle::Route>(route);
    style.arrowAtMidpoint = arrowAtMidpoint != 0;
    return std::isfinite(style.thickness) && style.thickness > 0.0f && std::isfinite(style.selectedThickness) &&
           style.selectedThickness > 0.0f && std::isfinite(style.bend) && style.bend >= 0.0f && route >= 0 &&
           route <= 1 && (arrowAtMidpoint == 0 || arrowAtMidpoint == 1);
}

bool ReadCount(std::istream& in, const char* expectedTag, std::size_t& count)
{
    std::string tag;
    return static_cast<bool>(in >> tag >> count) && tag == expectedTag && count <= kStyleEntryLimit;
}
} // namespace

const LXNodeStyle& LXStyleSheet::ForNodeType(const std::string& type) const
{
    const auto typeStyle = typeStyles_.find(type);
    return typeStyle != typeStyles_.end() ? typeStyle->second : defaultNode;
}

const LXNodeStyle& LXStyleSheet::ForNode(const Node& node) const
{
    const auto nodeStyle = nodeStyles_.find(node.id);
    if (nodeStyle != nodeStyles_.end())
    {
        return nodeStyle->second;
    }

    return ForNodeType(node.type);
}

const LXPinStyle& LXStyleSheet::ForPin(const Pin& pin) const
{
    const auto pinStyle = pinStyles_.find(pin.id);
    if (pinStyle != pinStyles_.end())
    {
        return pinStyle->second;
    }

    const auto typeStyle = pinTypeStyles_.find(pin.type);
    return typeStyle != pinTypeStyles_.end() ? typeStyle->second : defaultPin;
}

const LXWireStyle& LXStyleSheet::ForWire(const Link& link, PinType type) const
{
    const auto linkStyle = wireStyles_.find(link.id);
    if (linkStyle != wireStyles_.end())
    {
        return linkStyle->second;
    }

    const auto typeStyle = wireTypeStyles_.find(type);
    return typeStyle != wireTypeStyles_.end() ? typeStyle->second : defaultWire;
}

const LXItemStyle& LXStyleSheet::ForItem(const std::string& role) const
{
    const auto item = itemStyles_.find(role);
    return item == itemStyles_.end() ? defaultItem_ : item->second;
}

void LXStyleSheet::SetItemStyle(std::string role, const LXItemStyle& style)
{
    itemStyles_.insert_or_assign(std::move(role), style);
}

void LXStyleSheet::SetTypeStyle(std::string type, const LXNodeStyle& style)
{
    typeStyles_.insert_or_assign(std::move(type), style);
}

void LXStyleSheet::SetNodeStyle(Id node, const LXNodeStyle& style)
{
    nodeStyles_.insert_or_assign(node, style);
}

void LXStyleSheet::SetPinTypeStyle(PinType type, const LXPinStyle& style)
{
    pinTypeStyles_.insert_or_assign(type, style);
}

void LXStyleSheet::SetPinStyle(Id pin, const LXPinStyle& style)
{
    pinStyles_.insert_or_assign(pin, style);
}

void LXStyleSheet::SetWireTypeStyle(PinType type, const LXWireStyle& style)
{
    wireTypeStyles_.insert_or_assign(type, style);
}

void LXStyleSheet::SetWireStyle(Id link, const LXWireStyle& style)
{
    wireStyles_.insert_or_assign(link, style);
}

bool LXStyleSheet::ClearTypeStyle(const std::string& type)
{
    return typeStyles_.erase(type) != 0;
}

bool LXStyleSheet::ClearNodeStyle(Id node)
{
    return nodeStyles_.erase(node) != 0;
}

bool LXStyleSheet::ClearPinStyle(Id pin)
{
    return pinStyles_.erase(pin) != 0;
}

bool LXStyleSheet::ClearWireTypeStyle(PinType type)
{
    return wireTypeStyles_.erase(type) != 0;
}

bool LXStyleSheet::ClearWireStyle(Id link)
{
    return wireStyles_.erase(link) != 0;
}

bool LXStyleSheet::HasTypeStyle(const std::string& type) const
{
    return typeStyles_.contains(type);
}

bool LXStyleSheet::HasNodeStyle(Id node) const
{
    return nodeStyles_.contains(node);
}

bool LXStyleSheet::Save(const std::string& path, std::string* error) const
{
    const std::filesystem::path target = Utf8Path(path);
    const std::wstring suffix =
        L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(gSaveSequence.fetch_add(1));
    std::filesystem::path staged = target;
    staged += suffix;
    std::ofstream out(staged, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return Fail(error, "Cannot open style output file");
    }

    Write(out);
    out.flush();
    const bool written = out.good();
    out.close();
    if (!written)
    {
        std::filesystem::remove(staged);
        return Fail(error, "Cannot write style file");
    }
    const auto stagedName = staged.u8string();
    const auto verified = LoadExact(std::string(stagedName.begin(), stagedName.end()), error);
    if (!verified || !Equals(*verified))
    {
        std::filesystem::remove(staged);
        return Fail(error, "Staged style did not round-trip exactly");
    }
    std::filesystem::path backup = target;
    backup += L".bak";
    std::error_code fileError;
    if (std::filesystem::exists(target, fileError) && LoadExact(path, nullptr))
    {
        std::filesystem::path stagedBackup = backup;
        stagedBackup += suffix;
        std::filesystem::copy_file(target, stagedBackup, std::filesystem::copy_options::overwrite_existing, fileError);
        if (fileError ||
            !MoveFileExW(stagedBackup.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(stagedBackup);
            std::filesystem::remove(staged);
            return Fail(error, "Cannot create style backup");
        }
    }
    if (!MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(staged);
        return Fail(error, "Cannot replace style file");
    }
    return true;
}

void LXStyleSheet::Write(std::ostream& out) const
{
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "LXS 6\nC ";
    WriteStyle(out, canvas);
    out << "\nFR ";
    WriteStyle(out, frame);
    out << "\nDN ";
    WriteStyle(out, defaultNode);
    out << "\nDP ";
    WriteStyle(out, defaultPin);
    out << "\nDW ";
    WriteStyle(out, defaultWire);
    out << '\n';

    out << "NT " << typeStyles_.size() << '\n';
    for (const auto& [type, style] : typeStyles_)
    {
        out << std::quoted(type) << ' ';
        WriteStyle(out, style);
        out << '\n';
    }
    out << "NI " << nodeStyles_.size() << '\n';
    for (const auto& [id, style] : nodeStyles_)
    {
        out << id << ' ';
        WriteStyle(out, style);
        out << '\n';
    }
    out << "PT " << pinTypeStyles_.size() << '\n';
    for (const auto& [type, style] : pinTypeStyles_)
    {
        out << static_cast<int>(type) << ' ';
        WriteStyle(out, style);
        out << '\n';
    }
    out << "PI " << pinStyles_.size() << '\n';
    for (const auto& [id, style] : pinStyles_)
    {
        out << id << ' ';
        WriteStyle(out, style);
        out << '\n';
    }
    out << "WT " << wireTypeStyles_.size() << '\n';
    for (const auto& [type, style] : wireTypeStyles_)
    {
        out << static_cast<int>(type) << ' ';
        WriteStyle(out, style);
        out << '\n';
    }
    out << "WI " << wireStyles_.size() << '\n';
    for (const auto& [id, style] : wireStyles_)
    {
        out << id << ' ';
        WriteStyle(out, style);
        out << '\n';
    }
    out << "DI ";
    WriteStyle(out, defaultItem_);
    out << "\nIT " << itemStyles_.size() << '\n';
    for (const auto& [role, style] : itemStyles_)
    {
        out << std::quoted(role) << ' ';
        WriteStyle(out, style);
        out << '\n';
    }
}

std::optional<LXStyleSheet> LXStyleSheet::Load(const std::string& path, std::string* error)
{
    auto styles = LoadExact(path, error);
    if (styles)
    {
        return styles;
    }
    std::filesystem::path backup = Utf8Path(path);
    backup += L".bak";
    if (!std::filesystem::exists(backup))
    {
        return std::nullopt;
    }
    const auto backupName = backup.u8string();
    styles = LoadExact(std::string(backupName.begin(), backupName.end()), error);
    if (styles && error)
    {
        *error = "Recovered style from backup; primary file is damaged";
    }
    return styles;
}

std::optional<LXStyleSheet> LXStyleSheet::LoadExact(const std::string& path, std::string* error)
{
    std::ifstream in(Utf8Path(path), std::ios::binary);
    if (!in)
    {
        Fail(error, "Cannot open style input file");
        return std::nullopt;
    }

    const auto invalid = [&](const char* message) -> std::optional<LXStyleSheet> {
        Fail(error, message);
        return std::nullopt;
    };

    std::string magic;
    std::string tag;
    int version = 0;
    LXStyleSheet styles;
    if (!(in >> magic >> version) || magic != "LXS" || (version < 1 || version > 6) || !(in >> tag) || tag != "C" ||
        !ReadStyle(in, styles.canvas, version) || !(in >> tag) ||
        (version >= 4 && (tag != "FR" || !ReadStyle(in, styles.frame) || !(in >> tag))) || tag != "DN" ||
        !ReadStyle(in, styles.defaultNode, version) || !(in >> tag) || tag != "DP" ||
        !ReadStyle(in, styles.defaultPin, version) || !(in >> tag) || tag != "DW" ||
        !ReadStyle(in, styles.defaultWire, version))
    {
        return invalid("Invalid style header or defaults");
    }
    styles.sourceVersion_ = version;

    std::size_t count = 0;
    if (!ReadCount(in, "NT", count))
    {
        return invalid("Invalid node type style count");
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        std::string type;
        LXNodeStyle style;
        if (!(in >> std::quoted(type)) || type.empty() || !ReadStyle(in, style, version) ||
            !styles.typeStyles_.emplace(type, style).second)
        {
            return invalid("Invalid node type style");
        }
    }
    if (!ReadCount(in, "NI", count))
    {
        return invalid("Invalid node style count");
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        Id id = 0;
        LXNodeStyle style;
        if (!(in >> id) || !id || !ReadStyle(in, style, version) || !styles.nodeStyles_.emplace(id, style).second)
        {
            return invalid("Invalid node style");
        }
    }
    if (!ReadCount(in, "PT", count))
    {
        return invalid("Invalid pin type style count");
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        int type = -1;
        LXPinStyle style;
        if (!(in >> type) || type < 0 || type > static_cast<int>(PinType::Vector2) || !ReadStyle(in, style, version) ||
            !styles.pinTypeStyles_.emplace(static_cast<PinType>(type), style).second)
        {
            return invalid("Invalid pin type style");
        }
    }
    if (!ReadCount(in, "PI", count))
    {
        return invalid("Invalid pin style count");
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        Id id = 0;
        LXPinStyle style;
        if (!(in >> id) || !id || !ReadStyle(in, style, version) || !styles.pinStyles_.emplace(id, style).second)
        {
            return invalid("Invalid pin style");
        }
    }
    if (!ReadCount(in, "WT", count))
    {
        return invalid("Invalid wire type style count");
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        int type = -1;
        LXWireStyle style;
        if (!(in >> type) || type < 0 || type > static_cast<int>(PinType::Vector2) || !ReadStyle(in, style, version) ||
            !styles.wireTypeStyles_.emplace(static_cast<PinType>(type), style).second)
        {
            return invalid("Invalid wire type style");
        }
    }
    if (!ReadCount(in, "WI", count))
    {
        return invalid("Invalid link style count");
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        Id id = 0;
        LXWireStyle style;
        if (!(in >> id) || !id || !ReadStyle(in, style, version) || !styles.wireStyles_.emplace(id, style).second)
        {
            return invalid("Invalid link style");
        }
    }

    if (version >= 2)
    {
        if (!(in >> tag) || tag != "DI" || !ReadStyle(in, styles.defaultItem_) || !ReadCount(in, "IT", count))
        {
            return invalid("Invalid item style header");
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            std::string role;
            LXItemStyle style;
            if (!(in >> std::quoted(role)) || role.empty() || !ReadStyle(in, style) ||
                !styles.itemStyles_.emplace(role, style).second)
            {
                return invalid("Invalid item style");
            }
        }
    }

    in >> std::ws;
    return in.eof() ? std::optional<LXStyleSheet>(std::move(styles)) : invalid("Unexpected style data");
}

LXStyleSheet LXStyleSheet::WithoutIndividualOverrides() const
{
    LXStyleSheet view = *this;
    view.nodeStyles_.clear();
    view.pinStyles_.clear();
    view.wireStyles_.clear();
    return view;
}

bool LXStyleSheet::Equals(const LXStyleSheet& other) const
{
    std::ostringstream first;
    std::ostringstream second;
    Write(first);
    other.Write(second);
    return first.str() == second.str();
}

} // namespace LX
