// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBMovementConfig.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UVBMovementConfig::UVBMovementConfig()
{
	// 캡슐 속도는 각 팩 클립의 root 속도를 따른다. Fighter 오버라이드는 DA_Movement_Default 자산에 있고,
	// ArmedDefaultProfile 은 Magic 과 폴백용 jog 값을 유지한다.

	// Unarmed: run 500(직진)/350(strafe)/300(후진), sprint 균일 650(2단 램프 없음).
	UnarmedProfile.WalkSpeeds = FVector(200.f, 175.f, 150.f);
	UnarmedProfile.RunSpeeds = FVector(500.f, 350.f, 300.f);
	UnarmedProfile.RunDiagonalSpeed = 0.f; // Armed jog 보정만 소비한다.
	UnarmedProfile.SprintRunSpeeds = FVector(650.f, 650.f, 650.f);
	UnarmedProfile.SprintSpeeds = FVector(650.f, 650.f, 650.f); // 미사용(ramp off)이나 명료성 위해 명시
	UnarmedProfile.CrouchSpeed = 150.f;
	UnarmedProfile.bSprintRampEnabled = false;

	FVBMovementProfile Blade = ArmedDefaultProfile;
	Blade.RunDiagonalSpeed = 240.f;
	Blade.SprintSpeeds = FVector(680.f, 680.f, 680.f);
	WeaponProfileOverrides.Add(EVBWeaponType::Blade, Blade);

	// BigSword: jog254/대각선240, Run320, Fast487. 고속 Run/Fast는 별도 Sprint PSD에서 선택.
	// Walk/Crouch 는 Armed 디폴트와 같지만 완전한 프로파일로 다시 적는다.
	FVBMovementProfile Big = ArmedDefaultProfile;
	Big.RunSpeeds = FVector(255.f, 240.f, 225.f);
	Big.RunDiagonalSpeed = 240.f;
	Big.SprintRunSpeeds = FVector(485.f, 485.f, 485.f);
	Big.SprintSpeeds = Big.SprintRunSpeeds;
	Big.SprintPoolJoinSpeed = 400.f;
	Big.bSprintRampEnabled = false;
	WeaponProfileOverrides.Add(EVBWeaponType::BigSword, Big);
}

const FVBMovementProfile& UVBMovementConfig::ResolveProfile(EVBWeaponType WeaponType, bool bArmed) const
{
	if (!bArmed)
	{
		return UnarmedProfile;
	}

	if (const FVBMovementProfile* Ov = WeaponProfileOverrides.Find(WeaponType))
	{
		return *Ov;
	}

	return ArmedDefaultProfile;
}

#if WITH_EDITOR
EDataValidationResult UVBMovementConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	// 곡선이 없으면 옆, 뒤 걸음이 앞 속도로 돌아 모션 매칭 클립이 어긋난다.
	if (!StrafeSpeedMapCurve)
	{
		Context.AddError(NSLOCTEXT("VBMovementConfig", "NoStrafeCurve", "StrafeSpeedMapCurve 가 비었다 - 방향별 걷기 속도(앞 · 옆 · 뒤)를 정할 곡선을 넣는다."));
		Result = EDataValidationResult::Invalid;
	}
	// 끝 속도 >= 시작 속도면 슬라이드가 시작 프레임에 끝난다(런타임은 CanRequestSlide 가 아예 막는다).
	if (SlideEndSpeed >= SlideStartSpeed)
	{
		Context.AddError(FText::Format(NSLOCTEXT("VBMovementConfig", "SlideSpeedOrder",
			"SlideEndSpeed({0}) 는 SlideStartSpeed({1}) 보다 작아야 한다 - 같거나 크면 슬라이드가 시작 프레임에 끝난다."),
			FText::AsNumber(SlideEndSpeed), FText::AsNumber(SlideStartSpeed)));
		Result = EDataValidationResult::Invalid;
	}
	// 아날로그 걷기 문턱은 (0, 1] 이어야 한다. 0 이하면 스틱으로 걸을 수 없고, 1 을 넘으면 패드는 끝까지 기울여도 걷는다.
	if (AnalogWalkRunThreshold <= 0.f || AnalogWalkRunThreshold > 1.f)
	{
		Context.AddError(FText::Format(NSLOCTEXT("VBMovementConfig", "AnalogWalkRunThresholdRange",
			"AnalogWalkRunThreshold({0}) 는 0 보다 크고 1 이하여야 한다 - 밖이면 패드로 걸을 수 없거나 늘 걷는다."),
			FText::AsNumber(AnalogWalkRunThreshold)));
		Result = EDataValidationResult::Invalid;
	}
	// 회전 문턱은 (0, 180] - 0 이면 매 프레임 회전, 180 초과면 발이 안 돈다.
	if (TurnInPlaceYawThreshold <= 0.f || TurnInPlaceYawThreshold > 180.f)
	{
		Context.AddError(FText::Format(NSLOCTEXT("VBMovementConfig", "TurnInPlaceYawThresholdRange",
			"TurnInPlaceYawThreshold({0}) 는 0 보다 크고 180 이하여야 한다 - 0 이면 서 있는 동안 계속 돌고, 180 을 넘으면 제자리 회전이 없다."),
			FText::AsNumber(TurnInPlaceYawThreshold)));
		Result = EDataValidationResult::Invalid;
	}
	// 검사를 했으니 "안 함"(부모 기본값 NotValidated)으로 돌려주지 않는다 - 오류가 없으면 유효다.
	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif
