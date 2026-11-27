#pragma once

#include <cstddef>
#include <vector>

namespace lwe {

// Interaction souris native sur l'image finale (logique pure, testee) : ondulations au clic et trainee qui
// suit le curseur. Coordonnees en pixels du rendu, origine en haut a gauche.

struct Ripple {
    float x = 0.0f;
    float y = 0.0f;
    float age = 0.0f;  // secondes
};

class RippleSet {
public:
    static constexpr size_t kMaxRipples = 8;
    static constexpr float kLifetime = 1.6f;

    void Add(float x, float y);  // au-dela de kMaxRipples, la plus ancienne disparait
    void Advance(float seconds);
    void Clear() noexcept { ripples_.clear(); }
    const std::vector<Ripple>& Items() const noexcept { return ripples_; }
    bool Empty() const noexcept { return ripples_.empty(); }

private:
    std::vector<Ripple> ripples_;
};

struct TrailPoint {
    float x = 0.0f;
    float y = 0.0f;
    float age = 0.0f;
};

class CursorTrail {
public:
    static constexpr size_t kMaxPoints = 24;
    static constexpr float kLifetime = 0.45f;
    static constexpr float kMinSpacing = 3.0f;  // pixels entre deux points enregistres

    // Position du curseur a cette image ; `inside` faux : plus de nouveaux points (la trainee s'efface).
    void Update(float x, float y, bool inside, float seconds);
    void Clear() noexcept { points_.clear(); }
    const std::vector<TrailPoint>& Points() const noexcept { return points_; }  // du plus recent au plus ancien
    bool Empty() const noexcept { return points_.empty(); }

private:
    std::vector<TrailPoint> points_;
};

}  // namespace lwe
