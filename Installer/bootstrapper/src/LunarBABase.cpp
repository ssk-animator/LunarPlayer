#include "LunarBABase.h"

STDMETHODIMP LunarBABase::QueryInterface(REFIID riid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IBootstrapperApplication) || riid == IID_IUnknown) {
        *ppv = static_cast<IBootstrapperApplication*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) LunarBABase::AddRef() {
    return (ULONG)InterlockedIncrement(&m_ref);
}
STDMETHODIMP_(ULONG) LunarBABase::Release() {
    LONG r = InterlockedDecrement(&m_ref);
    if (r == 0) delete this;
    return (ULONG)r;
}
STDMETHODIMP LunarBABase::OnStartup() { return S_OK; }
STDMETHODIMP_(int) LunarBABase::OnShutdown() {
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnSystemShutdown(__in DWORD dwEndSession, __in int nRecommendation) {
    (void)dwEndSession;
    (void)nRecommendation;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnDetectBegin(__in BOOL fInstalled, __in DWORD cPackages) {
    (void)fInstalled;
    (void)cPackages;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnDetectForwardCompatibleBundle(__in_z LPCWSTR wzBundleId, __in BOOTSTRAPPER_RELATION_TYPE relationType, __in_z LPCWSTR wzBundleTag, __in BOOL fPerMachine, __in DWORD64 dw64Version, __in int nRecommendation) {
    (void)wzBundleId;
    (void)relationType;
    (void)wzBundleTag;
    (void)fPerMachine;
    (void)dw64Version;
    (void)nRecommendation;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnDetectUpdateBegin(__in_z LPCWSTR wzUpdateLocation, __in int nRecommendation) {
    (void)wzUpdateLocation;
    (void)nRecommendation;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnDetectUpdate(__in_z_opt LPCWSTR wzUpdateLocation, __in DWORD64 dw64Size, __in DWORD64 dw64Version, __in_z_opt LPCWSTR wzTitle, __in_z_opt LPCWSTR wzSummary, __in_z_opt LPCWSTR wzContentType, __in_z_opt LPCWSTR wzContent, __in int nRecommendation) {
    (void)wzUpdateLocation;
    (void)dw64Size;
    (void)dw64Version;
    (void)wzTitle;
    (void)wzSummary;
    (void)wzContentType;
    (void)wzContent;
    (void)nRecommendation;
    return IDNOACTION;
}
STDMETHODIMP_(void) LunarBABase::OnDetectUpdateComplete(__in HRESULT hrStatus, __in_z_opt LPCWSTR wzUpdateLocation) {
    (void)hrStatus;
    (void)wzUpdateLocation;
}
STDMETHODIMP_(int) LunarBABase::OnDetectRelatedBundle(__in_z LPCWSTR wzBundleId, __in BOOTSTRAPPER_RELATION_TYPE relationType, __in_z LPCWSTR wzBundleTag, __in BOOL fPerMachine, __in DWORD64 dw64Version, __in BOOTSTRAPPER_RELATED_OPERATION operation) {
    (void)wzBundleId;
    (void)relationType;
    (void)wzBundleTag;
    (void)fPerMachine;
    (void)dw64Version;
    (void)operation;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnDetectPackageBegin(__in_z LPCWSTR wzPackageId) {
    (void)wzPackageId;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnDetectCompatiblePackage(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzCompatiblePackageId) {
    (void)wzPackageId;
    (void)wzCompatiblePackageId;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnDetectRelatedMsiPackage(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzProductCode, __in BOOL fPerMachine, __in DWORD64 dw64Version, __in BOOTSTRAPPER_RELATED_OPERATION operation) {
    (void)wzPackageId;
    (void)wzProductCode;
    (void)fPerMachine;
    (void)dw64Version;
    (void)operation;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnDetectTargetMsiPackage(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzProductCode, __in BOOTSTRAPPER_PACKAGE_STATE patchState) {
    (void)wzPackageId;
    (void)wzProductCode;
    (void)patchState;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnDetectMsiFeature(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzFeatureId, __in BOOTSTRAPPER_FEATURE_STATE state) {
    (void)wzPackageId;
    (void)wzFeatureId;
    (void)state;
    return IDNOACTION;
}
STDMETHODIMP_(void) LunarBABase::OnDetectPackageComplete(__in_z LPCWSTR wzPackageId, __in HRESULT hrStatus, __in BOOTSTRAPPER_PACKAGE_STATE state) {
    (void)wzPackageId;
    (void)hrStatus;
    (void)state;
}
STDMETHODIMP_(void) LunarBABase::OnDetectComplete(__in HRESULT hrStatus) {
    (void)hrStatus;
}
STDMETHODIMP_(int) LunarBABase::OnPlanBegin(__in DWORD cPackages) {
    (void)cPackages;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnPlanRelatedBundle(__in_z LPCWSTR wzBundleId, __inout BOOTSTRAPPER_REQUEST_STATE* pRequestedState) {
    (void)wzBundleId;
    (void)pRequestedState;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnPlanPackageBegin(__in_z LPCWSTR wzPackageId, __inout BOOTSTRAPPER_REQUEST_STATE* pRequestedState) {
    (void)wzPackageId;
    (void)pRequestedState;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnPlanCompatiblePackage(__in_z LPCWSTR wzPackageId, __inout BOOTSTRAPPER_REQUEST_STATE* pRequestedState) {
    (void)wzPackageId;
    (void)pRequestedState;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnPlanTargetMsiPackage(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzProductCode, __inout BOOTSTRAPPER_REQUEST_STATE* pRequestedState) {
    (void)wzPackageId;
    (void)wzProductCode;
    (void)pRequestedState;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnPlanMsiFeature(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzFeatureId, __inout BOOTSTRAPPER_FEATURE_STATE* pRequestedState) {
    (void)wzPackageId;
    (void)wzFeatureId;
    (void)pRequestedState;
    return IDNOACTION;
}
STDMETHODIMP_(void) LunarBABase::OnPlanPackageComplete(__in_z LPCWSTR wzPackageId, __in HRESULT hrStatus, __in BOOTSTRAPPER_PACKAGE_STATE state, __in BOOTSTRAPPER_REQUEST_STATE requested, __in BOOTSTRAPPER_ACTION_STATE execute, __in BOOTSTRAPPER_ACTION_STATE rollback) {
    (void)wzPackageId;
    (void)hrStatus;
    (void)state;
    (void)requested;
    (void)execute;
    (void)rollback;
}
STDMETHODIMP_(void) LunarBABase::OnPlanComplete(__in HRESULT hrStatus) {
    (void)hrStatus;
}
STDMETHODIMP_(int) LunarBABase::OnApplyBegin() {
    return IDNOACTION;
}
STDMETHODIMP_(void) LunarBABase::OnApplyPhaseCount(__in DWORD dwPhaseCount) {
    (void)dwPhaseCount;
}
STDMETHODIMP_(int) LunarBABase::OnElevate() {
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnProgress(__in DWORD dwProgressPercentage, __in DWORD dwOverallPercentage) {
    (void)dwProgressPercentage;
    (void)dwOverallPercentage;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnError(__in BOOTSTRAPPER_ERROR_TYPE errorType, __in_z_opt LPCWSTR wzPackageId, __in DWORD dwCode, __in_z_opt LPCWSTR wzError, __in DWORD uiFlags, __in DWORD cData, __in_ecount_z_opt(cData) LPCWSTR* rgwzData, __in int nRecommendation) {
    (void)errorType;
    (void)wzPackageId;
    (void)dwCode;
    (void)wzError;
    (void)uiFlags;
    (void)cData;
    (void)rgwzData;
    (void)nRecommendation;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnRegisterBegin() {
    return IDNOACTION;
}
STDMETHODIMP_(void) LunarBABase::OnRegisterComplete(__in HRESULT hrStatus) {
    (void)hrStatus;
}
STDMETHODIMP_(int) LunarBABase::OnCacheBegin() {
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnCachePackageBegin(__in_z LPCWSTR wzPackageId, __in DWORD cCachePayloads, __in DWORD64 dw64PackageCacheSize) {
    (void)wzPackageId;
    (void)cCachePayloads;
    (void)dw64PackageCacheSize;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnCacheAcquireBegin(__in_z_opt LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId, __in BOOTSTRAPPER_CACHE_OPERATION operation, __in_z LPCWSTR wzSource) {
    (void)wzPackageOrContainerId;
    (void)wzPayloadId;
    (void)operation;
    (void)wzSource;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnCacheAcquireProgress(__in_z_opt LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId, __in DWORD64 dw64Progress, __in DWORD64 dw64Total, __in DWORD dwOverallPercentage) {
    (void)wzPackageOrContainerId;
    (void)wzPayloadId;
    (void)dw64Progress;
    (void)dw64Total;
    (void)dwOverallPercentage;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnResolveSource(__in_z LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId, __in_z LPCWSTR wzLocalSource, __in_z_opt LPCWSTR wzDownloadSource) {
    (void)wzPackageOrContainerId;
    (void)wzPayloadId;
    (void)wzLocalSource;
    (void)wzDownloadSource;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnCacheAcquireComplete(__in_z_opt LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId, __in HRESULT hrStatus, __in int nRecommendation) {
    (void)wzPackageOrContainerId;
    (void)wzPayloadId;
    (void)hrStatus;
    (void)nRecommendation;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnCacheVerifyBegin(__in_z_opt LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId) {
    (void)wzPackageOrContainerId;
    (void)wzPayloadId;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnCacheVerifyComplete(__in_z_opt LPCWSTR wzPackageOrContainerId, __in_z_opt LPCWSTR wzPayloadId, __in HRESULT hrStatus, __in int nRecommendation) {
    (void)wzPackageOrContainerId;
    (void)wzPayloadId;
    (void)hrStatus;
    (void)nRecommendation;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnCachePackageComplete(__in_z LPCWSTR wzPackageId, __in HRESULT hrStatus, __in int nRecommendation) {
    (void)wzPackageId;
    (void)hrStatus;
    (void)nRecommendation;
    return IDNOACTION;
}
STDMETHODIMP_(void) LunarBABase::OnCacheComplete(__in HRESULT hrStatus) {
    (void)hrStatus;
}
STDMETHODIMP_(int) LunarBABase::OnExecuteBegin(__in DWORD cExecutingPackages) {
    (void)cExecutingPackages;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnExecutePackageBegin(__in_z LPCWSTR wzPackageId, __in BOOL fExecute) {
    (void)wzPackageId;
    (void)fExecute;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnExecutePatchTarget(__in_z LPCWSTR wzPackageId, __in_z LPCWSTR wzTargetProductCode) {
    (void)wzPackageId;
    (void)wzTargetProductCode;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnExecuteProgress(__in_z LPCWSTR wzPackageId, __in DWORD dwProgressPercentage, __in DWORD dwOverallPercentage) {
    (void)wzPackageId;
    (void)dwProgressPercentage;
    (void)dwOverallPercentage;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnExecuteMsiMessage(__in_z LPCWSTR wzPackageId, __in INSTALLMESSAGE mt, __in UINT uiFlags, __in_z LPCWSTR wzMessage, __in DWORD cData, __in_ecount_z_opt(cData) LPCWSTR* rgwzData, __in int nRecommendation) {
    (void)wzPackageId;
    (void)mt;
    (void)uiFlags;
    (void)wzMessage;
    (void)cData;
    (void)rgwzData;
    (void)nRecommendation;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnExecuteFilesInUse(__in_z LPCWSTR wzPackageId, __in DWORD cFiles, __in_ecount_z(cFiles) LPCWSTR* rgwzFiles) {
    (void)wzPackageId;
    (void)cFiles;
    (void)rgwzFiles;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnExecutePackageComplete(__in_z LPCWSTR wzPackageId, __in HRESULT hrStatus, __in BOOTSTRAPPER_APPLY_RESTART restart, __in int nRecommendation) {
    (void)wzPackageId;
    (void)hrStatus;
    (void)restart;
    (void)nRecommendation;
    return IDNOACTION;
}
STDMETHODIMP_(void) LunarBABase::OnExecuteComplete(__in HRESULT hrStatus) {
    (void)hrStatus;
}
STDMETHODIMP_(void) LunarBABase::OnUnregisterBegin() {
}
STDMETHODIMP_(void) LunarBABase::OnUnregisterComplete(__in HRESULT hrStatus) {
    (void)hrStatus;
}
STDMETHODIMP_(int) LunarBABase::OnApplyComplete(__in HRESULT hrStatus, __in BOOTSTRAPPER_APPLY_RESTART restart) {
    (void)hrStatus;
    (void)restart;
    return IDNOACTION;
}
STDMETHODIMP_(int) LunarBABase::OnLaunchApprovedExeBegin() {
    return IDNOACTION;
}
STDMETHODIMP_(void) LunarBABase::OnLaunchApprovedExeComplete(__in HRESULT hrStatus, __in DWORD dwProcessId) {
    (void)hrStatus;
    (void)dwProcessId;
}
