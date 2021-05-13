#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MotionInterpolatorComponent.generated.h"

USTRUCT(BlueprintType)
struct FMotionSnapshot
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FTransform Transform;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FVector Velocity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FVector AngularVelocity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float Timestamp;

	FMotionSnapshot(FTransform newTransform, FVector newVelocity, FVector newAngularVelocity, float newTimestamp) :
		Transform(newTransform),
		Velocity(newVelocity),
		AngularVelocity(newAngularVelocity),
		Timestamp(newTimestamp)
	{}

	FMotionSnapshot() :
		Transform(),
		Velocity(),
		AngularVelocity(),
		Timestamp()
	{}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMotionInterpolatorDelegate, const FMotionSnapshot&, Snapshot);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PUNCHBAGONLINE_API UMotionInterpolatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UMotionInterpolatorComponent();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void SetComponentOverride(class USceneComponent* InComponentOverride);

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void AddSnapshot(const FMotionSnapshot& Snapshot);
	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void GetSnapshotAtTime(float TargetTime, bool CanExtrapolate, FMotionSnapshot& Result, int& OffBorder);

	UFUNCTION(Server, unreliable)
	void ServerSendSnapshot(const FMotionSnapshot& InSnapshot, ENetRole SenderRole);
	UFUNCTION(NetMulticast, unreliable)
	void MulticastSendSnapshot(const FMotionSnapshot& InSnapshot, ENetRole SenderRole);

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void ResetToNewestPredictedPose();

	UFUNCTION(BlueprintPure, Category = "MotionInterpolator")
	TArray<FMotionSnapshot> GetSnapshots() { return Snapshots; }

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void OvertakeMovementAuthority(float Duration, float BlendOutDuration);

	FMotionSnapshot Interpolate(const FMotionSnapshot& FirstSnapshot, const FMotionSnapshot& SecondSnapshot, float TargetTime);
	FMotionSnapshot SimpleInterpolate(const FMotionSnapshot& FirstSnapshot, const FMotionSnapshot& SecondSnapshot, float Alpha);
	FMotionSnapshot Extrapolate(const FMotionSnapshot& Snapshot, float TargetTime);

	UPROPERTY(BlueprintAssignable, Category = "MotionInterpolator")
	FMotionInterpolatorDelegate OnSnapshotAdded;

	UPROPERTY(EditAnywhere, Category = "MotionInterpolator")
	bool UseExtrapolation = false;

	UPROPERTY(EditAnywhere, Category = "MotionInterpolator")
	FName SyncedComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator", meta = (ExposeOnSpawn = true))
	int BufferSize = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float NetworkDelay = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float SyncPeriod = 0.1f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float SnapPeriod = 0.2f;

private:
	class USceneComponent* GetComponentToSync();

	class USceneComponent* ComponentToSync;
	class USceneComponent* ComponentOverride;
	TArray<FMotionSnapshot> Snapshots;
	float AuthorityReleaseTime = 0.0f;
	float AuthorityBlendOutFinishTime = 0.0f;
	float AuthorityBlendOutDuration = 0.0f;
	float LastSyncTime = 0.0f;
	float LastSnapTime = 0.0f;
};