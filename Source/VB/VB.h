// ProjectVB
// Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/VBLogContext.h"

// VB 로그 카테고리
DECLARE_LOG_CATEGORY_EXTERN(LogVB, Log, All);

// 진단 로그 간격 - Ready 가 true 면 지금 한 줄 찍고 다음 시각을 Interval 뒤로 민다. 간격은 선언에 한 번 적는다.
struct FVBLogGate
{
	double Interval = 1.0;
	double NextTime = 0.0;
	bool Ready(double Now) { if (Now < NextTime) return false; NextTime = Now + Interval; return true; }
};

// Shipping 빌드에서 완전 제거되는 디버그 로그 매크로
#if !UE_BUILD_SHIPPING
#define VB_LOG(Verbosity, Format, ...) \
	UE_LOG(LogVB, Verbosity, TEXT(Format), ##__VA_ARGS__);
#define VB_CLOG(Condition, Verbosity, Format, ...) \
	UE_CLOG(Condition, LogVB, Verbosity, TEXT(Format), ##__VA_ARGS__);
#define VB_LOG_CTX(Context, Verbosity, Format, ...) \
	UE_LOG(LogVB, Verbosity, TEXT("[%s] ") TEXT(Format), *VBGetLogContext(Context), ##__VA_ARGS__);
#else
#define VB_LOG(...)
#define VB_CLOG(...)
#define VB_LOG_CTX(...)
#endif
