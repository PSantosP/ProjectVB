#include "Enemy/VBEnemyMovementRules.h"

float VBEnemyMovementRules::CalculateGaitSpeed(EVBGait Gait, float WalkSpeed, float RunSpeed, float StrideScale)
{
	const float BaseSpeed = (Gait == EVBGait::Walk) ? WalkSpeed : RunSpeed;
	return BaseSpeed * StrideScale;
}
