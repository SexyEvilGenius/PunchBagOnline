#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MotionInterpolatorComponent.generated.h"

UENUM()
enum class EMotionSnapshotFlags : uint8
{
	None				= 0x0,
	HasVelocity			= 0x1,
	HasAngularVelocity	= 0x2,

	FLAGS_COUNT			= 0x2
};
ENUM_CLASS_FLAGS(EMotionSnapshotFlags)

USTRUCT(BlueprintType)
struct FMotionSnapshot
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FVector Location;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FRotator Rotation;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FVector Velocity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	FVector AngularVelocity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float Timestamp;

	float ArrivalTime = 0;

	FMotionSnapshot(FVector InLocation, FQuat InRotation, FVector InVelocity, FVector InAngularVelocity, float InTimestamp) :
		Location(InLocation),
		Rotation(InRotation),
		Velocity(InVelocity),
		AngularVelocity(InAngularVelocity),
		Timestamp(InTimestamp)
	{}

	FMotionSnapshot() :
		Location(),
		Rotation(),
		Velocity(),
		AngularVelocity(),
		Timestamp()
	{}

	FMotionSnapshot(const USceneComponent& InComponent, float InTimestamp);

	FMotionSnapshot(const USceneComponent& InComponent);

	void ApplyTo(USceneComponent& InComponent);

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FMotionSnapshot> : public TStructOpsTypeTraitsBase2<FMotionSnapshot>
{
	enum
	{
		WithNetSerializer = true
	};
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMotionInterpolatorDelegate, const FMotionSnapshot&, Snapshot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMotionInterpolatorErrorDelegate);
DECLARE_DELEGATE(FAdditionalDelayDelegate);

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

	UFUNCTION(Server, Unreliable)
	void ServerSendSnapshot(const FMotionSnapshot& InSnapshot, FGuid SenderGuid);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSendSnapshot(const FMotionSnapshot& InSnapshot, FGuid SenderGuid);
	UFUNCTION(Client, Unreliable)
	void ClientSendSnapshot(const FMotionSnapshot& InSnapshot);

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void ServerTakeOwnership_Implementation(AActor* newOwner, float OwnershipDuration);
	UFUNCTION(Server, Reliable)
	void ServerReleaseOwnership(const FMotionSnapshot& LatestSnapshot, float ClientNetworkDelay);
	UFUNCTION(Client, Reliable)
	void ClientReleaseOwnership();

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	TArray<FMotionSnapshot> GetSnapshots() { return Snapshots; }

	UFUNCTION(BlueprintPure, Category = "MotionInterpolator")
	float GetLookupTime();

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void EnableTempHighFreqUpdate();
	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void SetHighFreqUpdateEnabled(bool Enabled);

	UFUNCTION(BlueprintCallable, Category = "MotionInterpolator")
	void OvertakeMovementAuthority(float Duration);

	FMotionSnapshot Interpolate(const FMotionSnapshot& FirstSnapshot, const FMotionSnapshot& SecondSnapshot, float TargetTime);
	FMotionSnapshot SimpleInterpolate(const FMotionSnapshot& FirstSnapshot, const FMotionSnapshot& SecondSnapshot, float Alpha);
	FMotionSnapshot Extrapolate(const FMotionSnapshot& Snapshot, float TargetTime);

	UPROPERTY(BlueprintAssignable, Category = "MotionInterpolator")
	FMotionInterpolatorDelegate OnSnapshotAdded;
	UPROPERTY(BlueprintAssignable, Category = "MotionInterpolator")
	FMotionInterpolatorErrorDelegate OnNotEnoughData;

	UPROPERTY(EditAnywhere, Category = "MotionInterpolator")
	bool UseExtrapolation = false;

	UPROPERTY(EditAnywhere, Category = "MotionInterpolator")
	FName SyncedComponentName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator", meta = (ExposeOnSpawn = true))
	int BufferSize = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator", meta = (InlineEditConditionToggle))
	bool bUseFixedNetworkDelay = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator", meta = (EditCondition = "bUseFixedNetworkDelay"))
	float NetworkDelay = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator", meta = (EditCondition = "!bUseFixedNetworkDelay"), AdvancedDisplay)
	float NetworkDelayInterpolationSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float SyncPeriod = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float HighFreqSyncPeriod = 0.01f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float HighFreqSyncDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float SnapPeriod = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionInterpolator")
	float AuthorityBlendTime = 0.5f;

private:
	class USceneComponent* GetComponentToSync();
	float GetSnapshotsMaxDelay();
	float GetLookupTimeOffset();
	const class AGameStateBase* GetGameState();
	class USceneComponent* ComponentToSync;
	class USceneComponent* ComponentOverride;
	TArray<FMotionSnapshot> Snapshots;
	FGuid GUID = FGuid::NewGuid();
	float AuthorityReleaseTime = 0.0f;
	float CurrentAuthorityBlendTime = 0.0f;
	float LastSyncTime = 0.0f;
	float LastSnapTime = 0.0f;
	float TargetNetworkDelay = 0.0f;
	float CurrentOwnershipDuration = 0.0f;
	float CurrentAdditionalNetworkDelay = 0.0f;
	float TargetAdditionalNetworkDelay = 0.0f;
	float CurrentHightFreqSyncDuration = 0.0f;
	FAdditionalDelayDelegate OnAdditionalDelayReached;
	bool HadMovementAuthority = false;
	const class AGameStateBase* CachedGameState = nullptr;
};