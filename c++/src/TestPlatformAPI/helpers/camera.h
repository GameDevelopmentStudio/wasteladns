#ifndef __WASTELADNS_CAMERA_H__
#define __WASTELADNS_CAMERA_H__

#ifndef UNITYBUILD
#include <cstring>
#include "transform.h"
#endif

struct Camera {
	Transform transform;
	Mat4 viewMatrix;
};

namespace CameraSystem {
    
};

#endif // __WASTELADNS_CAMERA_H__
