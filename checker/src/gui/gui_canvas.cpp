#include "checker/gui.h"

#include <algorithm>
#include <cmath>

namespace checker::gui {

Canvas2D::Canvas2D() {
    origin_ = ImGui::GetCursorScreenPos();
    size_ = ImGui::GetContentRegionAvail();
    if (size_.x < 80.0f) {
        size_.x = 80.0f;
    }
    if (size_.y < 80.0f) {
        size_.y = 80.0f;
    }

    ImGui::InvisibleButton("checker_canvas", size_);
    draw_list_ = ImGui::GetWindowDrawList();
    draw_list_->AddRectFilled(
        origin_,
        ImVec2(origin_.x + size_.x, origin_.y + size_.y),
        IM_COL32(18, 20, 23, 255));
    draw_list_->AddRect(
        origin_,
        ImVec2(origin_.x + size_.x, origin_.y + size_.y),
        IM_COL32(80, 84, 92, 255));
}

void Canvas2D::fit_bbox(double minx, double miny, double maxx, double maxy) {
    if (!std::isfinite(minx) || !std::isfinite(miny) ||
        !std::isfinite(maxx) || !std::isfinite(maxy)) {
        return;
    }
    if (maxx <= minx) {
        maxx = minx + 1.0;
    }
    if (maxy <= miny) {
        maxy = miny + 1.0;
    }
    minx_ = minx;
    miny_ = miny;
    maxx_ = maxx;
    maxy_ = maxy;
}

ImVec2 Canvas2D::world_to_screen(double x, double y) const {
    constexpr float pad = 18.0f;
    const double sx = (x - minx_) / (maxx_ - minx_);
    const double sy = (y - miny_) / (maxy_ - miny_);
    const float px = origin_.x + pad + static_cast<float>(sx) * std::max(1.0f, size_.x - 2.0f * pad);
    const float py = origin_.y + size_.y - pad -
        static_cast<float>(sy) * std::max(1.0f, size_.y - 2.0f * pad);
    return ImVec2(px, py);
}

void Canvas2D::segment(
    double x1,
    double y1,
    double x2,
    double y2,
    ImU32 color,
    float thickness) {
    draw_list_->AddLine(world_to_screen(x1, y1), world_to_screen(x2, y2), color, thickness);
}

void Canvas2D::point(double x, double y, float radius, ImU32 color) {
    draw_list_->AddCircleFilled(world_to_screen(x, y), radius, color);
}

void Canvas2D::label(double x, double y, const char* text, ImU32 color) {
    ImVec2 p = world_to_screen(x, y);
    p.x += 5.0f;
    p.y -= 5.0f;
    draw_list_->AddText(p, color, text);
}

void Canvas2D::grid(ImU32 color, double step) {
    if (step <= 0.0 || !std::isfinite(step)) {
        return;
    }
    const double start_x = std::ceil(minx_ / step) * step;
    for (double x = start_x; x <= maxx_; x += step) {
        segment(x, miny_, x, maxy_, color, 1.0f);
    }
    const double start_y = std::ceil(miny_ / step) * step;
    for (double y = start_y; y <= maxy_; y += step) {
        segment(minx_, y, maxx_, y, color, 1.0f);
    }
}

} // namespace checker::gui
