#include "valor/anim.h"

#include <algorithm>

#include "valor/fixed.h"

namespace valor {

namespace {

// pose_build's root offset during a fade, k quarters toward the target.
void blend_root(const int16_t a[3], const int16_t t[3], int k, int16_t out[3]) {
    for (int i = 0; i < 3; i++) {
        int32_t x = a[i], y = t[i];
        out[i] = k == 0 ? a[i] : k == 1 ? s16(x + ((y - x) >> 2)) : k == 2 ? s16(x + ((y - x) >> 1)) : s16(y - ((y - x) >> 2));
    }
}

}  // namespace

void AnimInstance::start(int n, int a, int b, int mode) {
    if (!model || n < 0 || size_t(n) >= model->anims.size() || model->anims[n].frames.empty()) return;
    int count = int(model->anims[n].frames.size());
    anim = int16_t(n), frame = int16_t(std::min(a, count - 1));
    first = int16_t(std::max(a, 3));
    target = b < 0 ? int16_t(count - 1) : int16_t(std::min(b, count - 1));
    if (target == frame) target = -1;
    if (mode == 1) {
        saved = pose;
        std::copy(root, root + 3, saved_root);
        blend = 4;
        blend_frame = -1;
    } else if (mode == 2) {
        blend = 0;
    } else if (blend > 0) {
        blend--;
    }
    // The frame's root offset (anim_root_motion: +0x4 into +0x98).
    Models::unpack3(model->anims[anim].frames[frame].root, root);
}

bool AnimInstance::advance() {
    if (anim < 0 || frame >= target) return false;
    frame++;
    if (blend > 0) blend--;
    Models::unpack3(model->anims[anim].frames[frame].root, root);
    return true;
}

void AnimInstance::build(const Models& models) {
    if (!model || anim < 0) {
        cache_anim = cache_frame = -1;
        return;
    }
    const Anim& a = model->anims[anim];
    if (blend > 0 && cache_anim != -1 && posed) {
        int tgt = blend_frame >= 0 ? blend_frame : first;
        tgt = std::min(tgt, int(a.frames.size()) - 1);
        int k = 4 - blend;
        int16_t troot[3];
        Models::unpack3(a.frames[tgt].root, troot);
        int16_t r[3];
        blend_root(saved_root, troot, k, r);
        std::copy(r, r + 3, root);
        if (k == 0) {
            pose = saved;
        } else {
            std::vector<Quat> b = models.frame_pose(*model, anim, tgt);
            std::vector<Quat> out;
            for (size_t i = 0; i < b.size(); i++) out.push_back(i < saved.size() ? quat_interp(models.gte, saved[i], b[i], k) : b[i]);
            pose = out;
        }
        cache_anim = anim, cache_frame = int16_t(tgt);
        nbones = a.frames[tgt].nquats;
    } else {
        if (blend > 0) blend = 0;  // a fade started with no pose yet
        if (anim != cache_anim || frame != cache_frame || !posed) {
            pose = models.frame_pose(*model, anim, frame);
            cache_anim = anim, cache_frame = frame;
        }
        nbones = a.frames[frame].nquats;
    }
    posed = true;
    joints = skeleton_joints(*model, pose, root, nbones);
}

}  // namespace valor
