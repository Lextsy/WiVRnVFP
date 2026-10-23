/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "wave_runtime.h"
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

namespace wave
{
glm::mat4 matrix_from_wave(const wvr::Matrix4f & matrix)
{
	// Wave matrices are row-major (m[row][col]); glm is column-major.
	glm::mat4 result(1.0f);
	for (int col = 0; col < 4; ++col)
		for (int row = 0; row < 4; ++row)
			result[col][row] = matrix.m[row][col];
	return result;
}
glm::mat4 pose_to_matrix(const wvr::PoseState & pose)
{
	// Wave matrices are row-major (m[row][col]); glm is column-major.
	glm::mat4 result(1.0f);
	for (int col = 0; col < 4; ++col)
		for (int row = 0; row < 4; ++row)
			result[col][row] = pose.poseMatrix.m[row][col];
	return result;
}

glm::mat4 pose_to_matrix(const wvr::Pose & pose)
{
	glm::mat4 result(1.0f);
	glm::quat rotation(pose.rotation.w, pose.rotation.x, pose.rotation.y, pose.rotation.z);
	result = glm::mat4_cast(rotation);
	result[3] = glm::vec4(pose.position.v[0], pose.position.v[1], pose.position.v[2], 1.0f);
	return result;
}

glm::quat matrix_to_quaternion(const glm::mat4 & m)
{
	// Column-major rotation matrix -> quaternion.
	float trace = m[0][0] + m[1][1] + m[2][2];
	if (trace > 0.0f)
	{
		float s = 0.5f / std::sqrt(trace + 1.0f);
		return glm::quat{0.25f / s, (m[2][1] - m[1][2]) * s, (m[0][2] - m[2][0]) * s, (m[1][0] - m[0][1]) * s};
	}
	else if (m[0][0] > m[1][1] and m[0][0] > m[2][2])
	{
		float s = 2.0f * std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]);
		return glm::quat{(m[2][1] - m[1][2]) / s, 0.25f * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s};
	}
	else if (m[1][1] > m[2][2])
	{
		float s = 2.0f * std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]);
		return glm::quat{(m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25f * s, (m[1][2] + m[2][1]) / s};
	}
	else
	{
		float s = 2.0f * std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]);
		return glm::quat{(m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25f * s};
	}
}

XrVector3f matrix_translation(const glm::mat4 & m)
{
	return XrVector3f{m[3][0], m[3][1], m[3][2]};
}

XrFovf fov_from_projection(const wvr::Matrix4f & projection, float & near, float & far)
{
	// OpenGL perspective projection, column-major:
	//   m[0][0] = 2n/(r-l), m[1][1] = 2n/(t-b), m[2][0] = (r+l)/(r-l),
	//   m[2][2] = -(f+n)/(f-n), m[2][3] = -2fn/(f-n)
	float a = 1.0f / projection.m[0][0];
	float b = projection.m[2][0];
	float A = projection.m[2][2];
	float B = projection.m[2][3];

	near = B / (A - 1.0f);
	far = near * (A - 1.0f) / (A + 1.0f);

	return XrFovf{
	        .angleLeft = std::atan(a * (b - 1.0f)),
	        .angleRight = std::atan(a * (b + 1.0f)),
	        .angleUp = std::atan(1.0f / projection.m[1][1]),
	        .angleDown = -std::atan(1.0f / projection.m[1][1]),
	};
}
} // namespace wave
