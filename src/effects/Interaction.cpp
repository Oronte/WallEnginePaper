#include "effects/Interaction.h"

#include <algorithm>
#include <cmath>

namespace lwe {

void RippleSet::Add(float x, float y) {
    if (ripples_.size() >= kMaxRipples) {
        // La plus ancienne (age le plus grand) laisse sa place.
        const auto oldest = std::max_element(ripples_.begin(), ripples_.end(),
                                             [](const Ripple& a, const Ripple& b) { return a.age < b.age; });
        ripples_.erase(oldest);
    }
    ripples_.push_back(Ripple{x, y, 0.0f});
}

void RippleSet::Advance(float seconds) {
    for (auto& ripple : ripples_) {
        ripple.age += std::max(0.0f, seconds);
    }
    std::erase_if(ripples_, [](const Ripple& ripple) { return ripple.age >= kLifetime; });
}

void CursorTrail::Update(float x, float y, bool inside, float seconds) {
    for (auto& point : points_) {
        point.age += std::max(0.0f, seconds);
    }
    std::erase_if(points_, [](const TrailPoint& point) { return point.age >= kLifetime; });
    if (!inside) {
        return;
    }
    if (!points_.empty()) {
        const TrailPoint& last = points_.front();
        if (std::hypot(x - last.x, y - last.y) < kMinSpacing) {
            return;  // curseur immobile : rien de nouveau, la trainee s'estompe
        }
    }
    points_.insert(points_.begin(), TrailPoint{x, y, 0.0f});
    if (points_.size() > kMaxPoints) {
        points_.resize(kMaxPoints);
    }
}

}  // namespace lwe
