#pragma once
#include <raylib.h>
class World;

bool VoxelRaycastSmooth(World& world, Vector3 origin, Vector3 dir, float max_dist,
                        Vector3& out_hit, Vector3& out_solid, Vector3& out_empty);
