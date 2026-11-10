// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Async/Future.h"
#include "HAL/PlatformProcess.h"

// 한 번의 실행에서 telemetry.jsonl, video.mp4, audio.wav 를 쓴다(telemetry 계약 v1).
// 영상 프레임마다 엔진 frame id 를 픽셀 마커로 찍어 ffmpeg 에 넘기므로 빠지거나 겹친 프레임이 드러난다.
// -DeterministicAudio 면 프레임마다 오디오 한 프레임을 직접 렌더해 audio.wav 가 영상과 같은 게임 시계로 간다.
struct FVBPlayBotNativeCapture
{
	double StartSeconds = 0.0;
	double IntervalSeconds = 0.0;
	double MaxGapSeconds = 0.0;
	int64 MaxBytes = 0;
	int32 MaxFrames = 0;
};

class FVBPlayBotRecorder
{
public:
	~FVBPlayBotRecorder();

	bool Open(const FString& OutDir, const FString& InFFmpegPath, FString& OutError);
	bool OpenNative(const FString& OutDir, const FVBPlayBotNativeCapture& Limits, FString& OutError);
	// 실제 초로 간격을 재고, 미완료 요청은 하나만 유지한다.
	void RequestNativeFrame(int32 WorldEpoch, bool bAllowNewRequest = true);
	// 마지막 이미지 작업을 비우고 최종 캡처 수를 확정한다.
	bool FinalizeNativeCapture();
	void WriteLine(const FString& Json);
	// 프레임마다 한 번 - runner 의 진행 없음 감시가 파일 크기를 읽고, 멈춰도 마지막 줄을 잃지 않아야 한다.
	void Flush() { if (Telemetry) Telemetry->Flush(); }

	void StartCapture();
	// 녹화 중 프레임마다 한 번. 이미지는 같은 프레임 안에서 나중에 온다.
	void RequestFrame();
	void StopCapture();
	// encoder 를 비우고 파일을 닫는다. 두 번 불러도 안전하다.
	void Close();

	bool HasFailed() const { return !Failure.IsEmpty(); }
	bool HasPendingNativeFrame() const { return bNativePending || !NativeJobs.IsEmpty(); }
	const FString& GetFailure() const { return Failure; }
	int64 GetCapturedFrames() const { return VideoFrame; }

	// analyze.py 와 공유하는 마커 배치: 가드 블록, 32 비트 블록(LSB 먼저), 가드 블록. 각 8x8 px.
	static constexpr int32 MarkerBlock = 8;
	static constexpr int32 MarkerBits = 32;
	static void StampMarker(TArray<FColor>& Pixels, int32 Width, int32 Height, uint32 Value);

private:
	bool PumpAudio(float RealDeltaSeconds);
	void StartAudio();
	// 녹음을 멈추고 돌아오기 전에 audio.wav 를 쓴다. 두 번 불러도 안전하다.
	void FinishAudio();
	void HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Colors);
	void WriteNativeFrame(int32 Width, int32 Height, const TArray<FColor>& Colors);
	void PumpNativeFrames(bool bDrain);
	struct FNativeFrameResult
	{
		FString Error;
		int64 Bytes = 0;
		double WorkMs = 0.0;
	};
	struct FNativeFrameJob
	{
		int64 Index = 0;
		uint64 EngineFrame = 0;
		int32 Epoch = 0;
		int64 ReservedBytes = 0;
		double Seconds = 0.0;
		double MainThreadMs = 0.0;
		FString Relative;
		TFuture<FNativeFrameResult> Future;
	};
	TArray<FNativeFrameJob> NativeJobs;
	int64 NativeRequestedFrames = 0;
	int64 NativeReservedBytes = 0;
	bool StartEncoder(int32 Width, int32 Height);
	bool WritePipeAll(const uint8* Data, int32 Size);

	bool bNative = false;
	bool bNativePending = false;
	FVBPlayBotNativeCapture Native;
	double NativeRequestSeconds = -1.0;
	double LastNativeCaptureSeconds = -1.0;
	int64 NativeBytes = 0;
	int32 NativeEpoch = 0;
	FString Dir;
	FString FFmpegPath;
	TUniquePtr<FArchive> Telemetry;
	FDelegateHandle ScreenshotHandle;
	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;
	FProcHandle Encoder;
	FIntPoint FrameSize = FIntPoint::ZeroValue;
	TArray<FColor> Scratch;
	int64 VideoFrame = 0;
	bool bCapturing = false;
	FString Failure;
	FTSTicker::FDelegateHandle AudioTicker;
	// recorder 가 오디오 렌더를 넘겨받기 전의 au.nrt.RenderEveryTick(Close 에서 되돌린다).
	int32 PreviousRenderEveryTick = 1;
	// 넘겨받기 전의 au.NeverDisableSubmixes(Close 에서 되돌린다).
	int32 PreviousNeverDisableSubmixes = 0;
	// recorder 가 non-realtime mixer 를 직접 렌더하기 시작하면 true(엔진의 tick 마다 렌더는 꺼진다).
	bool bAudioTakenOver = false;
	bool bRecordingAudio = false;
	double AudioRenderedSeconds = 0.0;
};
