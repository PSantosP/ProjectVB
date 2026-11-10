// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "PlayBot/VBPlayBotInputDriver.h"

struct FVBPlayBotSteeringView
{
	FVector Delta = FVector::ZeroVector;
	double MoveYaw = 0.0;
	double CameraYaw = 0.0;
};

// 이동과 전투가 같은 패드 좌표 변환과 보정 시계를 사용한다.
class FVBPlayBotNativeSteering
{
public:
	void Drive(const FVBPlayBotSteeringView& View, double Magnitude, double Seconds,
		FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human)
	{
		if (View.Delta.IsNearlyZero()) { Input.SetStickWant(FVector2D::ZeroVector); Input.SetLookWant(FVector2D::ZeroVector); return; }
		if (Seconds >= NextCorrection)
		{
			DirectionError = Human.SampleDirectionErrorDeg();
			NextCorrection = Seconds + Human.SampleCorrectionIntervalMs()/1000.0;
		}
		const FVector Direction = View.Delta.GetSafeNormal2D().RotateAngleAxis(DirectionError,FVector::UpVector);
		const double Yaw = FMath::DegreesToRadians(View.MoveYaw);
		const FVector2D Forward(FMath::Cos(Yaw),FMath::Sin(Yaw));
		const FVector2D Right(-FMath::Sin(Yaw),FMath::Cos(Yaw));
		const FVector2D Move(Direction.X,Direction.Y);
		Input.SetStickWant(FVector2D(FVector2D::DotProduct(Move,Right),FVector2D::DotProduct(Move,Forward))*Magnitude);
		Input.SetLookWant(FVector2D(FMath::Clamp(FMath::FindDeltaAngleDegrees(View.CameraYaw,View.Delta.Rotation().Yaw)/70.0,-0.35,0.35),0.0));
	}
private:
	double NextCorrection = 0.0;
	double DirectionError = 0.0;
};
