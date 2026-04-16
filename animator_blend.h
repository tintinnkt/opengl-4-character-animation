#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <learnopengl/animation.h>
#include <learnopengl/bone.h>

// ─────────────────────────────────────────────────────────────────────────────
// AnimatorBlend
//
// Drop-in replacement for the learnopengl Animator that adds cross-fade blending.
//
// How it works:
//   - When PlayAnimation() is called, we snapshot the current bone matrices.
//   - We start the new animation from t=0 in parallel.
//   - For BLEND_TIME seconds we LERP each bone matrix between the snapshot
//     and the live new-animation pose.
//   - After BLEND_TIME the snapshot is discarded and only the new anim runs.
//
// Usage is identical to the original Animator:
//   AnimatorBlend animator(idleAnim);
//   animator.UpdateAnimation(dt);
//   animator.PlayAnimation(walkAnim);   // triggers a smooth cross-fade
//   auto& matrices = animator.GetFinalBoneMatrices();
// ─────────────────────────────────────────────────────────────────────────────

class AnimatorBlend
{
public:
    static constexpr float BLEND_TIME = 0.2f;   // seconds for cross-fade

    AnimatorBlend(Animation* animation)
    {
        m_CurrentTime      = 0.0f;
        m_CurrentAnimation = animation;
        m_BlendTime        = 0.0f;
        m_IsBlending       = false;

        m_FinalBoneMatrices.resize(100, glm::mat4(1.0f));
        m_SnapshotMatrices .resize(100, glm::mat4(1.0f));
        m_NewBoneMatrices  .resize(100, glm::mat4(1.0f));
    }

    // ── Start a cross-fade to a new animation ────────────────────────────────
    void PlayAnimation(Animation* next)
    {
        if (next == m_CurrentAnimation) return;

        // Snapshot current final pose
        m_SnapshotMatrices = m_FinalBoneMatrices;

        // Switch to new anim, reset its time
        m_CurrentAnimation = next;
        m_CurrentTime      = 0.0f;

        // Start blend
        m_BlendTime  = 0.0f;
        m_IsBlending = true;
    }

    // ── Call every frame ─────────────────────────────────────────────────────
    void UpdateAnimation(float dt)
    {
        if (!m_CurrentAnimation) return;

        // Advance new animation time
        m_CurrentTime += m_CurrentAnimation->GetTicksPerSecond() * dt;
        m_CurrentTime  = fmod(m_CurrentTime, m_CurrentAnimation->GetDuration());

        if (m_IsBlending)
        {
            m_BlendTime += dt;
            float t = glm::clamp(m_BlendTime / BLEND_TIME, 0.0f, 1.0f);
            t = smoothstep(t);   // ease in/out

            // Compute NEW animation pose into m_NewBoneMatrices
            CalculateBoneTransform(
                &m_CurrentAnimation->GetRootNode(),
                glm::mat4(1.0f),
                m_NewBoneMatrices);

            // Lerp snapshot → new pose
            for (int i = 0; i < 100; ++i)
                m_FinalBoneMatrices[i] = lerpMat4(m_SnapshotMatrices[i],
                                                   m_NewBoneMatrices[i], t);

            if (m_BlendTime >= BLEND_TIME)
                m_IsBlending = false;
        }
        else
        {
            // Normal single-anim update
            CalculateBoneTransform(
                &m_CurrentAnimation->GetRootNode(),
                glm::mat4(1.0f),
                m_FinalBoneMatrices);
        }
    }

    const std::vector<glm::mat4>& GetFinalBoneMatrices() const
    {
        return m_FinalBoneMatrices;
    }

private:
    // ── Bone transform traversal (writes into target vector) ─────────────────
    void CalculateBoneTransform(const AssimpNodeData* node,
                                glm::mat4 parentTransform,
                                std::vector<glm::mat4>& target)
    {
        const std::string& nodeName    = node->name;
        glm::mat4          nodeTransform = node->transformation;

        Bone* bone = m_CurrentAnimation->FindBone(nodeName);
        if (bone)
        {
            bone->Update(m_CurrentTime);
            nodeTransform = bone->GetLocalTransform();
        }

        glm::mat4 globalTransform = parentTransform * nodeTransform;

        auto& boneInfoMap = m_CurrentAnimation->GetBoneIDMap();
        auto  it          = boneInfoMap.find(nodeName);
        if (it != boneInfoMap.end())
        {
            int       index  = it->second.id;
            glm::mat4 offset = it->second.offset;
            if (index < 100)
                target[index] = globalTransform * offset;
        }

        for (int i = 0; i < node->childrenCount; ++i)
            CalculateBoneTransform(&node->children[i], globalTransform, target);
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    // Smooth-step easing for the blend factor
    static float smoothstep(float t)
    {
        return t * t * (3.0f - 2.0f * t);
    }

    // Component-wise lerp between two mat4s
    static glm::mat4 lerpMat4(const glm::mat4& a, const glm::mat4& b, float t)
    {
        glm::mat4 result;
        for (int col = 0; col < 4; ++col)
            result[col] = glm::mix(a[col], b[col], t);
        return result;
    }

    // ── State ─────────────────────────────────────────────────────────────────
    Animation*             m_CurrentAnimation;
    float                  m_CurrentTime;

    bool                   m_IsBlending;
    float                  m_BlendTime;

    std::vector<glm::mat4> m_FinalBoneMatrices;   // output → GPU
    std::vector<glm::mat4> m_SnapshotMatrices;    // pose at blend start
    std::vector<glm::mat4> m_NewBoneMatrices;     // incoming anim pose
};
