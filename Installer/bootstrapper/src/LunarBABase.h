#pragma once
// AUTO-GENERATED from WiX v3.14 IBootstrapperApplication.h
// Default (do-nothing) base. LunarBA overrides the lifecycle
// methods it actually drives. Do not hand-edit.
#include <windows.h>
#include <msi.h>
#include <objbase.h>
#include "IBootstrapperEngine.h"
#include "IBootstrapperApplication.h"

class LunarBABase : public IBootstrapperApplication {
public:
    LunarBABase() : m_ref(1) {}
    virtual ~LunarBABase() = default;
    STDMETHODIMP QueryInterface(REFIID riid, LPVOID* ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP OnStartup() override;
    STDMETHODIMP_(int) OnShutdown() override;
    STDMETHODIMP_(int) OnSystemShutdown(__in DWORD dwEndSession, __in int nRecommendation) override;
    STDMETHODIMP_(int) OnDetectBegin(__in BOOL fInstalled, __in DWORD cPackages) override;
    STDMETHODIMP_(int) OnDetectForwardCompatibleBundle(__in_z LPCWSTR wzBundleId, __in BOOTSTRAPPER_RELATION_TYPE relationType, __in_z LPCWSTR wzBundleTag, __in BOOL fPerMachine, __in DWORD64 dw64Version, __in int nRecommendation) override;
    STDMETHODIMP_(int) OnDetectUpdateBegin(__in_z LPCWSTR wzUpdateLocation, __in int nRecommendation) override;
    STDMETHODIMP_(int) OnDetectUpdate(__in_z_opt LPCWSTR wzUpdateLocation, __in DWORD64 dw64Size, __in DWORD64 dw64Version, __in_z_opt LPCWSTR wzTitle, __in_z_opt LPCWSTR wzSummary, __in_z_opt LPCWSTR wzContentType, __in_z_opt LPCWSTR wzContent, __in int nRecommendation) override;
    STDMETHODIMP_(void) OnDetectUpdateComplete(__in HRESULT hrStatus, __in_z_opt LPCWSTR wzUpdateLocation) override;
    STDMETHODIMP_(int) OnDetectRelatedBundle(__in_z LPCWSTR wzBundleId, __in BOOTSTRAPPER_RELATION_TYPE relationType, __in_z LPCWSTR wzBundleTag, __in BOOL fPerMachine, __in DWORD64 dw64Version, __in BOOTSTRAPPER_RELATED_OPERATION operation) override;
    STDMETHODIMP_(int) OnDetectPackageBegin(__in_z LPCWSTR wzPackageId) override;
    STDMETHODIMP_(int) OnDetectCompatiblePackage(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzCompatiblePackageId) override;
    STDMETHODIMP_(int) OnDetectRelatedMsiPackage(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzProductCode, __in BOOL fPerMachine, __in DWORD64 dw64Version, __in BOOTSTRAPPER_RELATED_OPERATION operation) override;
    STDMETHODIMP_(int) OnDetectTargetMsiPackage(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzProductCode, __in BOOTSTRAPPER_PACKAGE_STATE patchState) override;
    STDMETHODIMP_(int) OnDetectMsiFeature(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzFeatureId, __in BOOTSTRAPPER_FEATURE_STATE state) override;
    STDMETHODIMP_(void) OnDetectPackageComplete(__in_z LPCWSTR wzPackageId, __in HRESULT hrStatus, __in BOOTSTRAPPER_PACKAGE_STATE state) override;
    STDMETHODIMP_(void) OnDetectComplete(__in HRESULT hrStatus) override;
    STDMETHODIMP_(int) OnPlanBegin(__in DWORD cPackages) override;
    STDMETHODIMP_(int) OnPlanRelatedBundle(__in_z LPCWSTR wzBundleId, __inout BOOTSTRAPPER_REQUEST_STATE* pRequestedState) override;
    STDMETHODIMP_(int) OnPlanPackageBegin(__in_z LPCWSTR wzPackageId, __inout BOOTSTRAPPER_REQUEST_STATE* pRequestedState) override;
    STDMETHODIMP_(int) OnPlanCompatiblePackage(__in_z LPCWSTR wzPackageId, __inout BOOTSTRAPPER_REQUEST_STATE* pRequestedState) override;
    STDMETHODIMP_(int) OnPlanTargetMsiPackage(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzProductCode, __inout BOOTSTRAPPER_REQUEST_STATE* pRequestedState) override;
    STDMETHODIMP_(int) OnPlanMsiFeature(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzFeatureId, __inout BOOTSTRAPPER_FEATURE_STATE* pRequestedState) override;
    STDMETHODIMP_(void) OnPlanPackageComplete(__in_z LPCWSTR wzPackageId, __in HRESULT hrStatus, __in BOOTSTRAPPER_PACKAGE_STATE state, __in BOOTSTRAPPER_REQUEST_STATE requested, __in BOOTSTRAPPER_ACTION_STATE execute, __in BOOTSTRAPPER_ACTION_STATE rollback) override;
    STDMETHODIMP_(void) OnPlanComplete(__in HRESULT hrStatus) override;
    STDMETHODIMP_(int) OnApplyBegin() override;
    STDMETHODIMP_(void) OnApplyPhaseCount(__in DWORD dwPhaseCount) override;
    STDMETHODIMP_(int) OnElevate() override;
    STDMETHODIMP_(int) OnProgress(__in DWORD dwProgressPercentage, __in DWORD dwOverallPercentage) override;
    STDMETHODIMP_(int) OnError(__in BOOTSTRAPPER_ERROR_TYPE errorType, __in_z_opt LPCWSTR wzPackageId, __in DWORD dwCode, __in_z_opt LPCWSTR wzError, __in DWORD uiFlags, __in DWORD cData, __in_ecount_z_opt(cData) LPCWSTR* rgwzData, __in int nRecommendation) override;
    STDMETHODIMP_(int) OnRegisterBegin() override;
    STDMETHODIMP_(void) OnRegisterComplete(__in HRESULT hrStatus) override;
    STDMETHODIMP_(int) OnCacheBegin() override;
    STDMETHODIMP_(int) OnCachePackageBegin(__in_z LPCWSTR wzPackageId, __in DWORD cCachePayloads, __in DWORD64 dw64PackageCacheSize) override;
    STDMETHODIMP_(int) OnCacheAcquireBegin(__in_z_opt LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId, __in BOOTSTRAPPER_CACHE_OPERATION operation, __in_z LPCWSTR wzSource) override;
    STDMETHODIMP_(int) OnCacheAcquireProgress(__in_z_opt LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId, __in DWORD64 dw64Progress, __in DWORD64 dw64Total, __in DWORD dwOverallPercentage) override;
    STDMETHODIMP_(int) OnResolveSource(__in_z LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId, __in_z LPCWSTR wzLocalSource, __in_z_opt LPCWSTR wzDownloadSource) override;
    STDMETHODIMP_(int) OnCacheAcquireComplete(__in_z_opt LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId, __in HRESULT hrStatus, __in int nRecommendation) override;
    STDMETHODIMP_(int) OnCacheVerifyBegin(__in_z_opt LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId) override;
    STDMETHODIMP_(int) OnCacheVerifyComplete(__in_z_opt LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId, __in HRESULT hrStatus, __in int nRecommendation) override;
    STDMETHODIMP_(int) OnCachePackageComplete(__in_z LPCWSTR wzPackageId, __in HRESULT hrStatus, __in int nRecommendation) override;
    STDMETHODIMP_(void) OnCacheComplete(__in HRESULT hrStatus) override;
    STDMETHODIMP_(int) OnExecuteBegin(__in DWORD cExecutingPackages) override;
    STDMETHODIMP_(int) OnExecutePackageBegin(__in_z LPCWSTR wzPackageId, __in BOOL fExecute) override;
    STDMETHODIMP_(int) OnExecutePatchTarget(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzTargetProductCode) override;
    STDMETHODIMP_(int) OnExecuteProgress(__in_z LPCWSTR wzPackageId, __in DWORD dwProgressPercentage, __in DWORD dwOverallPercentage) override;
    STDMETHODIMP_(int) OnExecuteMsiMessage(__in_z LPCWSTR wzPackageId, __in INSTALLMESSAGE mt, __in UINT uiFlags, __in_z LPCWSTR wzMessage, __in DWORD cData, __in_ecount_z_opt(cData) LPCWSTR* rgwzData, __in int nRecommendation) override;
    STDMETHODIMP_(int) OnExecuteFilesInUse(__in_z LPCWSTR wzPackageId, __in DWORD cFiles, __in_ecount_z(cFiles) LPCWSTR* rgwzFiles) override;
    STDMETHODIMP_(int) OnExecutePackageComplete(__in_z LPCWSTR wzPackageId, __in HRESULT hrStatus, __in BOOTSTRAPPER_APPLY_RESTART restart, __in int nRecommendation) override;
    STDMETHODIMP_(void) OnExecuteComplete(__in HRESULT hrStatus) override;
    STDMETHODIMP_(void) OnUnregisterBegin() override;
    STDMETHODIMP_(void) OnUnregisterComplete(__in HRESULT hrStatus) override;
    STDMETHODIMP_(int) OnApplyComplete(__in HRESULT hrStatus, __in BOOTSTRAPPER_APPLY_RESTART restart) override;
    STDMETHODIMP_(int) OnLaunchApprovedExeBegin() override;
    STDMETHODIMP_(void) OnLaunchApprovedExeComplete(__in HRESULT hrStatus, __in DWORD dwProcessId) override;
protected:
    LONG m_ref;
};
