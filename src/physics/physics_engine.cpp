#include "physics_engine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

void PhysicsEngine::init() {
    clearColliders();
}

void PhysicsEngine::swapGeometry(PhysicsEngine& other) noexcept {
    m_colliders.swap(other.m_colliders);m_triangles.swap(other.m_triangles);
    m_triangleGrid.swap(other.m_triangleGrid);m_largeTriangles.swap(other.m_largeTriangles);
    std::swap(m_cellFloorZ,other.m_cellFloorZ);std::swap(m_cellMinX,other.m_cellMinX);std::swap(m_cellMaxX,other.m_cellMaxX);
    std::swap(m_cellMinY,other.m_cellMinY);std::swap(m_cellMaxY,other.m_cellMaxY);
}

void PhysicsEngine::reserveGeometry(size_t triangles) {
    m_triangles.reserve(triangles);m_triangleGrid.reserve(4096);
}

void PhysicsEngine::clearColliders() {
    m_colliders.clear();
    m_triangles.clear();
    m_triangleGrid.clear();
    m_largeTriangles.clear();
}

void PhysicsEngine::addBoxCollider(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    ColliderAABB box;
    box.minX = std::min(minX, maxX);
    box.maxX = std::max(minX, maxX);
    box.minY = std::min(minY, maxY);
    box.maxY = std::max(minY, maxY);
    box.minZ = std::min(minZ, maxZ);
    box.maxZ = std::max(minZ, maxZ);
    box.isWalkableFloor = (box.maxZ - box.minZ < 30.0f);
    m_colliders.push_back(box);
}

void PhysicsEngine::buildCollidersFromCell(float floorZ, float minX, float maxX, float minY, float maxY) {
    clearColliders();
    m_cellFloorZ = floorZ;
    m_cellMinX = minX;
    m_cellMaxX = maxX;
    m_cellMinY = minY;
    m_cellMaxY = maxY;

    // 1. Solid floor
    addBoxCollider(minX - 200.0f, minY - 200.0f, floorZ - 100.0f,
                   maxX + 200.0f, maxY + 200.0f, floorZ);

    // 2. Solid ceiling (height ~ 240 units)
    addBoxCollider(minX - 200.0f, minY - 200.0f, floorZ + 240.0f,
                   maxX + 200.0f, maxY + 200.0f, floorZ + 350.0f);

    // 3. Perimeter walls (thickness 80 units)
    // West wall (-X)
    addBoxCollider(minX - 80.0f, minY - 80.0f, floorZ, minX, maxY + 80.0f, floorZ + 250.0f);
    // East wall (+X)
    addBoxCollider(maxX, minY - 80.0f, floorZ, maxX + 80.0f, maxY + 80.0f, floorZ + 250.0f);
    // South wall (-Y)
    addBoxCollider(minX - 80.0f, minY - 80.0f, floorZ, maxX + 80.0f, minY, floorZ + 250.0f);
    // North wall (+Y)
    addBoxCollider(minX - 80.0f, maxY, floorZ, maxX + 80.0f, maxY + 80.0f, floorZ + 250.0f);

}

static uint64_t gridKey(int32_t x, int32_t y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
}

void PhysicsEngine::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
    Vec3 v{c.x - a.x, c.y - a.y, c.z - a.z};
    Vec3 normal{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    float length = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (!std::isfinite(length) || length < 0.0001f) return;
    normal.x /= length; normal.y /= length; normal.z /= length;
    Triangle triangle{a, b, c, normal,
        {std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})},
        {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})}};
    uint32_t index = m_triangles.size();
    m_triangles.push_back(triangle);
    int32_t minX = floorf(triangle.minimum.x / 256.0f), maxX = floorf(triangle.maximum.x / 256.0f);
    int32_t minY = floorf(triangle.minimum.y / 256.0f), maxY = floorf(triangle.maximum.y / 256.0f);
    if (static_cast<int64_t>(maxX - minX + 1) * (maxY - minY + 1) > 64) {
        m_largeTriangles.push_back(index);
    } else {
        for (int32_t gx = minX; gx <= maxX; ++gx)
            for (int32_t gy = minY; gy <= maxY; ++gy)
                m_triangleGrid[gridKey(gx, gy)].push_back(index);
    }
}

void PhysicsEngine::queryTriangles(float x, float y, float radius, std::vector<uint32_t>& result) const {
    result = m_largeTriangles;
    int32_t minX = floorf((x - radius) / 256.0f), maxX = floorf((x + radius) / 256.0f);
    int32_t minY = floorf((y - radius) / 256.0f), maxY = floorf((y + radius) / 256.0f);
    for (int32_t gx = minX; gx <= maxX; ++gx) {
        for (int32_t gy = minY; gy <= maxY; ++gy) {
            auto found = m_triangleGrid.find(gridKey(gx, gy));
            if (found != m_triangleGrid.end()) result.insert(result.end(), found->second.begin(), found->second.end());
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
}

bool PhysicsEngine::triangleHeight(const Triangle& t, float x, float y, float& height) const {
    if (fabsf(t.normal.z) < 0.65f || x < t.minimum.x || x > t.maximum.x || y < t.minimum.y || y > t.maximum.y) return false;
    float ux = t.b.x - t.a.x, uy = t.b.y - t.a.y;
    float vx = t.c.x - t.a.x, vy = t.c.y - t.a.y;
    float denominator = ux * vy - uy * vx;
    if (fabsf(denominator) < 0.0001f) return false;
    float dx = x - t.a.x, dy = y - t.a.y;
    float u = (dx * vy - dy * vx) / denominator;
    float v = (ux * dy - uy * dx) / denominator;
    if (u < -0.0001f || v < -0.0001f || u + v > 1.0001f) return false;
    height = t.a.z + u * (t.b.z - t.a.z) + v * (t.c.z - t.a.z);
    return true;
}

bool PhysicsEngine::groundHeight(float x, float y, float startZ, float& height) const {
    if (m_triangles.empty()) { height = m_cellFloorZ; return true; }
    std::vector<uint32_t> candidates;
    queryTriangles(x, y, 0.0f, candidates);
    bool found = false;
    height = startZ - 10000.0f;
    for (uint32_t index : candidates) {
        float z;
        if (triangleHeight(m_triangles[index], x, y, z) && z <= startZ + 0.01f && z > height) {
            height = z;
            found = true;
        }
    }
    return found;
}

bool PhysicsEngine::findStandingPosition(float& x, float& y, float& z) {
    // Exported marker positions can overlap furniture or sit on the edge of a floor.
    const float originX = x, originY = y, originZ = z;
    for (int ring = 0; ring <= 12; ++ring) {
        int samples = ring == 0 ? 1 : 16;
        for (int sample = 0; sample < samples; ++sample) {
            float angle = sample * (6.2831853f / samples);
            float px = originX + cosf(angle) * ring * PLAYER_RADIUS;
            float py = originY + sinf(angle) * ring * PLAYER_RADIUS;
            float floor;
            if (!groundHeight(px, py, originZ + STEP_HEIGHT, floor) || floor < originZ - 512.0f) continue;
            float tx = px, ty = py, tz = floor, velocity = 0.0f;
            bool grounded = true;
            for (int tick = 0; tick < 4; ++tick)
                stepWorldPlayer(tx, ty, tz, velocity, 0, 0, 0, grounded, false, 1.0f / 60.0f);
            if (!grounded || fabsf(tx - px) > 0.1f || fabsf(ty - py) > 0.1f || fabsf(tz - floor) > 0.1f) continue;
            float hx, hy, hz, nx, ny, nz;
            if (raycast(px, py, floor + 0.5f, 0, 0, 1, PLAYER_HEIGHT,
                        hx, hy, hz, nx, ny, nz)) continue;
            x = px; y = py; z = floor;
            return true;
        }
    }
    return false;
}

void PhysicsEngine::stepWorldPlayer(float& x, float& y, float& z, float& velocity,
                                    float moveX, float moveY, float speed,
                                    bool& grounded, bool jump, float dt) {
    if (jump && grounded) { velocity = JUMP_VELOCITY; grounded = false; }
    int steps = std::max(1, static_cast<int>(ceilf(speed * dt / (PLAYER_RADIUS * 0.5f))));
    steps = std::min(steps, 16);
    float step = dt / steps;
    for (int s = 0; s < steps; ++s) {
        float oldX = x, oldY = y, oldZ = z;
        x += moveX * speed * step;
        y += moveY * speed * step;
        std::vector<uint32_t> candidates;
        queryTriangles(x, y, PLAYER_RADIUS, candidates);
        for (int iteration = 0; iteration < 3; ++iteration) {
            for (uint32_t index : candidates) {
                const Triangle& t = m_triangles[index];
                float low = z + STEP_HEIGHT + 0.1f, high = z + PLAYER_HEIGHT - 0.1f;
                if (fabsf(t.normal.z) >= 0.65f || t.maximum.z < low || t.minimum.z > high) continue;
                float levels[] = {low, (low + high) * 0.5f, high,
                    std::max(low, std::min(high, (t.minimum.z + t.maximum.z) * 0.5f))};
                for (float level : levels) {
                    Vec3 points[6];
                    int count = 0;
                    Vec3 vertices[] = {t.a, t.b, t.c};
                    for (int edge = 0; edge < 3; ++edge) {
                        const Vec3& a = vertices[edge];
                        const Vec3& b = vertices[(edge + 1) % 3];
                        if (level < std::min(a.z, b.z) || level > std::max(a.z, b.z)) continue;
                        if (fabsf(b.z - a.z) < 0.0001f) {
                            points[count++] = a; points[count++] = b;
                        } else {
                            float f = (level - a.z) / (b.z - a.z);
                            points[count++] = {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, level};
                        }
                    }
                    if (count < 2) continue;
                    int endpoint = 1;
                    float longest = 0.0f;
                    for (int i = 1; i < count; ++i) {
                        float dx = points[i].x - points[0].x, dy = points[i].y - points[0].y;
                        float length = dx * dx + dy * dy;
                        if (length > longest) { longest = length; endpoint = i; }
                    }
                    if (longest < 0.0001f) continue;
                    float dx = points[endpoint].x - points[0].x, dy = points[endpoint].y - points[0].y;
                    float f = std::max(0.0f, std::min(1.0f, ((x - points[0].x) * dx + (y - points[0].y) * dy) / longest));
                    float nx = x - points[0].x - dx * f, ny = y - points[0].y - dy * f;
                    float distance = sqrtf(nx * nx + ny * ny);
                    if (distance >= PLAYER_RADIUS) continue;
                    if (distance < 0.0001f) {
                        nx = t.normal.x; ny = t.normal.y;
                        if ((oldX - t.a.x) * nx + (oldY - t.a.y) * ny < 0) { nx = -nx; ny = -ny; }
                        float length = sqrtf(nx * nx + ny * ny);
                        nx /= length; ny /= length;
                    } else { nx /= distance; ny /= distance; }
                    x += nx * (PLAYER_RADIUS - distance + 0.01f);
                    y += ny * (PLAYER_RADIUS - distance + 0.01f);
                }
            }
        }
        velocity += GRAVITY * step;
        z += velocity * step;
        grounded = false;
        for (uint32_t index : candidates) {
            float surface;
            if (!triangleHeight(m_triangles[index], x, y, surface)) continue;
            if (velocity <= 0.0f && surface <= oldZ + STEP_HEIGHT && z <= surface && oldZ >= surface - STEP_HEIGHT) {
                z = surface; velocity = 0.0f; grounded = true;
            } else if (velocity > 0.0f && surface >= oldZ + PLAYER_HEIGHT && surface <= z + PLAYER_HEIGHT) {
                z = surface - PLAYER_HEIGHT; velocity = 0.0f;
            }
        }
    }
}

bool PhysicsEngine::checkCapsuleCollision(float x, float y, float z, float radius, float height,
                                         float& resolveX, float& resolveY, float& resolveZ) {
    resolveX = 0.0f;
    resolveY = 0.0f;
    resolveZ = 0.0f;
    bool collided = false;

    float capBottomZ = z;
    float capTopZ    = z + height;

    for (const auto& box : m_colliders) {
        // Broadphase Z check
        if (capTopZ < box.minZ || capBottomZ > box.maxZ) continue;

        // Closest point on 2D AABB in XY
        float closestX = std::max(box.minX, std::min(x, box.maxX));
        float closestY = std::max(box.minY, std::min(y, box.maxY));

        float dx = x - closestX;
        float dy = y - closestY;
        float distSq = dx * dx + dy * dy;

        if (distSq < radius * radius) {
            float dist = sqrtf(distSq);
            if (dist > 0.0001f) {
                float penetration = radius - dist;
                float nx = dx / dist;
                float ny = dy / dist;
                resolveX += nx * penetration;
                resolveY += ny * penetration;
            } else {
                // Completely inside box: push towards nearest face
                float dLeft   = fabsf(x - box.minX);
                float dRight  = fabsf(box.maxX - x);
                float dBottom = fabsf(y - box.minY);
                float dTop    = fabsf(box.maxY - y);

                float minD = std::min({dLeft, dRight, dBottom, dTop});
                if (minD == dLeft)        resolveX -= (radius + dLeft);
                else if (minD == dRight)  resolveX += (radius + dRight);
                else if (minD == dBottom) resolveY -= (radius + dBottom);
                else                      resolveY += (radius + dTop);
            }
            collided = true;
        }
    }

    return collided;
}

void PhysicsEngine::stepPlayer(float& posX, float& posY, float& posZ, float& velZ,
                              float moveDirX, float moveDirY, float speed,
                              bool& isGrounded, bool jumpRequested, float deltaTime) {
    if (!m_triangles.empty()) {
        stepWorldPlayer(posX, posY, posZ, velZ, moveDirX, moveDirY, speed,
                        isGrounded, jumpRequested, std::max(0.0f, std::min(deltaTime, 0.1f)));
        return;
    }
    // 1. Horizontal Movement with step climbing & wall sliding
    float targetDx = moveDirX * speed * deltaTime;
    float targetDy = moveDirY * speed * deltaTime;

    // Move in X
    if (fabsf(targetDx) > 0.0001f) {
        float testX = posX + targetDx;
        float resX = 0.0f, resY = 0.0f, resZ = 0.0f;
        if (checkCapsuleCollision(testX, posY, posZ + STEP_HEIGHT, PLAYER_RADIUS, PLAYER_HEIGHT - STEP_HEIGHT, resX, resY, resZ)) {
            // Apply wall slide resolution
            posX = testX + resX;
        } else {
            posX = testX;
        }
    }

    // Move in Y
    if (fabsf(targetDy) > 0.0001f) {
        float testY = posY + targetDy;
        float resX = 0.0f, resY = 0.0f, resZ = 0.0f;
        if (checkCapsuleCollision(posX, testY, posZ + STEP_HEIGHT, PLAYER_RADIUS, PLAYER_HEIGHT - STEP_HEIGHT, resX, resY, resZ)) {
            // Apply wall slide resolution
            posY = testY + resY;
        } else {
            posY = testY;
        }
    }

    // 2. Vertical Movement & Gravity
    if (jumpRequested && isGrounded) {
        velZ = JUMP_VELOCITY;
        isGrounded = false;
    }

    velZ += GRAVITY * deltaTime;
    posZ += velZ * deltaTime;

    // Floor collision detection
    float currentFloor = m_cellFloorZ;

    // Check if on top of any box colliders
    for (const auto& box : m_colliders) {
        if (posX >= box.minX - PLAYER_RADIUS * 0.5f && posX <= box.maxX + PLAYER_RADIUS * 0.5f &&
            posY >= box.minY - PLAYER_RADIUS * 0.5f && posY <= box.maxY + PLAYER_RADIUS * 0.5f) {
            if (box.maxZ <= posZ + STEP_HEIGHT && box.maxZ > currentFloor) {
                currentFloor = box.maxZ;
            }
        }
    }

    if (posZ <= currentFloor) {
        posZ = currentFloor;
        velZ = 0.0f;
        isGrounded = true;
    } else {
        isGrounded = false;
    }
}

bool PhysicsEngine::raycast(float startX, float startY, float startZ,
                           float dirX, float dirY, float dirZ, float maxDist,
                           float& hitX, float& hitY, float& hitZ,
                           float& hitNormalX, float& hitNormalY, float& hitNormalZ) {
    float closestDist = maxDist;
    bool hasHit = false;

    // Normalize dir
    float len = sqrtf(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (!std::isfinite(len) || len <= 0.0001f || maxDist <= 0.0f) return false;
    dirX /= len; dirY /= len; dirZ /= len;

    if (!m_triangles.empty()) {
        for (const auto& t : m_triangles) {
            Vec3 e1{t.b.x - t.a.x, t.b.y - t.a.y, t.b.z - t.a.z};
            Vec3 e2{t.c.x - t.a.x, t.c.y - t.a.y, t.c.z - t.a.z};
            Vec3 p{dirY * e2.z - dirZ * e2.y, dirZ * e2.x - dirX * e2.z, dirX * e2.y - dirY * e2.x};
            float determinant = e1.x * p.x + e1.y * p.y + e1.z * p.z;
            if (fabsf(determinant) < 0.00001f) continue;
            float inverse = 1.0f / determinant;
            Vec3 origin{startX - t.a.x, startY - t.a.y, startZ - t.a.z};
            float u = (origin.x * p.x + origin.y * p.y + origin.z * p.z) * inverse;
            if (u < 0.0f || u > 1.0f) continue;
            Vec3 q{origin.y * e1.z - origin.z * e1.y, origin.z * e1.x - origin.x * e1.z, origin.x * e1.y - origin.y * e1.x};
            float v = (dirX * q.x + dirY * q.y + dirZ * q.z) * inverse;
            if (v < 0.0f || u + v > 1.0f) continue;
            float distance = (e2.x * q.x + e2.y * q.y + e2.z * q.z) * inverse;
            if (distance <= 0.001f || distance >= closestDist) continue;
            closestDist = distance;
            hitNormalX = t.normal.x; hitNormalY = t.normal.y; hitNormalZ = t.normal.z;
            if (hitNormalX * dirX + hitNormalY * dirY + hitNormalZ * dirZ > 0.0f) {
                hitNormalX = -hitNormalX; hitNormalY = -hitNormalY; hitNormalZ = -hitNormalZ;
            }
            hasHit = true;
        }
        if (hasHit) {
            hitX = startX + dirX * closestDist;
            hitY = startY + dirY * closestDist;
            hitZ = startZ + dirZ * closestDist;
        }
        return hasHit;
    }

    // Test floor
    if (dirZ < -0.0001f && startZ > m_cellFloorZ) {
        float t = (m_cellFloorZ - startZ) / dirZ;
        if (t > 0.0f && t < closestDist) {
            closestDist = t;
            hitX = startX + dirX * t;
            hitY = startY + dirY * t;
            hitZ = m_cellFloorZ;
            hitNormalX = 0.0f; hitNormalY = 0.0f; hitNormalZ = 1.0f;
            hasHit = true;
        }
    }

    // Test box colliders
    for (const auto& box : m_colliders) {
        float tmin = 0.0f;
        float tmax = closestDist;

        float nX = 0.0f, nY = 0.0f, nZ = 0.0f;

        // X slab
        if (fabsf(dirX) > 0.00001f) {
            float t1 = (box.minX - startX) / dirX;
            float t2 = (box.maxX - startX) / dirX;
            float tNear = std::min(t1, t2);
            float tFar  = std::max(t1, t2);
            if (tNear > tmin) {
                tmin = tNear;
                nX = (dirX < 0.0f) ? 1.0f : -1.0f;
                nY = 0.0f; nZ = 0.0f;
            }
            tmax = std::min(tmax, tFar);
            if (tmin > tmax) continue;
        } else if (startX < box.minX || startX > box.maxX) continue;

        // Y slab
        if (fabsf(dirY) > 0.00001f) {
            float t1 = (box.minY - startY) / dirY;
            float t2 = (box.maxY - startY) / dirY;
            float tNear = std::min(t1, t2);
            float tFar  = std::max(t1, t2);
            if (tNear > tmin) {
                tmin = tNear;
                nX = 0.0f;
                nY = (dirY < 0.0f) ? 1.0f : -1.0f;
                nZ = 0.0f;
            }
            tmax = std::min(tmax, tFar);
            if (tmin > tmax) continue;
        } else if (startY < box.minY || startY > box.maxY) continue;

        // Z slab
        if (fabsf(dirZ) > 0.00001f) {
            float t1 = (box.minZ - startZ) / dirZ;
            float t2 = (box.maxZ - startZ) / dirZ;
            float tNear = std::min(t1, t2);
            float tFar  = std::max(t1, t2);
            if (tNear > tmin) {
                tmin = tNear;
                nX = 0.0f; nY = 0.0f;
                nZ = (dirZ < 0.0f) ? 1.0f : -1.0f;
            }
            tmax = std::min(tmax, tFar);
            if (tmin > tmax) continue;
        } else if (startZ < box.minZ || startZ > box.maxZ) continue;

        if (tmin < closestDist && tmin > 0.0f) {
            closestDist = tmin;
            hitX = startX + dirX * tmin;
            hitY = startY + dirY * tmin;
            hitZ = startZ + dirZ * tmin;
            hitNormalX = nX;
            hitNormalY = nY;
            hitNormalZ = nZ;
            hasHit = true;
        }
    }

    return hasHit;
}
