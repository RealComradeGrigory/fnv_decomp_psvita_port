#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include "../nif/nif_loader.h"

struct ColliderAABB {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    bool isWalkableFloor;
};

class PhysicsEngine {
public:
    PhysicsEngine() = default;
    ~PhysicsEngine() = default;

    void init();
    void clearColliders();
    void swapGeometry(PhysicsEngine& other) noexcept;
    void reserveGeometry(size_t triangles);

    // Add static collider (AABB in world space)
    void addBoxCollider(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);

    // Build colliders from cell object bounds
    void buildCollidersFromCell(float floorZ, float minX, float maxX, float minY, float maxY);
    void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c);
    bool groundHeight(float x, float y, float startZ, float& height) const;
    bool findStandingPosition(float& x, float& y, float& z);
    bool hasWorldGeometry() const { return !m_triangles.empty(); }

    // Player capsule physics simulation step
    // Resolves collisions against static world colliders with wall sliding & step climbing
    void stepPlayer(float& posX, float& posY, float& posZ, float& velZ,
                    float moveDirX, float moveDirY, float speed,
                    bool& isGrounded, bool jumpRequested, float deltaTime);

    // Raycast against static colliders (for bullet hits & interactions)
    bool raycast(float startX, float startY, float startZ,
                 float dirX, float dirY, float dirZ, float maxDist,
                 float& hitX, float& hitY, float& hitZ, float& hitNormalX, float& hitNormalY, float& hitNormalZ);

    // Tuning constants (Fallout: New Vegas scale: ~70 units = 1 meter)
    static constexpr float PLAYER_RADIUS = 22.0f;     // ~0.31m radius
    static constexpr float PLAYER_HEIGHT = 135.0f;    // ~1.92m height
    static constexpr float EYE_HEIGHT    = 124.0f;    // ~1.77m eye level
    static constexpr float STEP_HEIGHT   = 22.0f;     // Stair step climbing
    static constexpr float GRAVITY       = -680.0f;   // Units/s^2
    static constexpr float JUMP_VELOCITY = 265.0f;    // Units/s

private:
    std::vector<ColliderAABB> m_colliders;
    struct Triangle {
        Vec3 a, b, c, normal;
        Vec3 minimum, maximum;
    };
    std::vector<Triangle> m_triangles;
    std::unordered_map<uint64_t, std::vector<uint32_t>> m_triangleGrid;
    std::vector<uint32_t> m_largeTriangles;
    void queryTriangles(float x, float y, float radius, std::vector<uint32_t>& result) const;
    bool triangleHeight(const Triangle& triangle, float x, float y, float& height) const;
    void stepWorldPlayer(float& x, float& y, float& z, float& velocity,
                         float moveX, float moveY, float speed,
                         bool& grounded, bool jump, float dt);
    float m_cellFloorZ = 7360.0f;
    float m_cellMinX = 0.0f, m_cellMaxX = 0.0f;
    float m_cellMinY = 0.0f, m_cellMaxY = 0.0f;

    bool checkCapsuleCollision(float x, float y, float z, float radius, float height,
                               float& resolveX, float& resolveY, float& resolveZ);
};
