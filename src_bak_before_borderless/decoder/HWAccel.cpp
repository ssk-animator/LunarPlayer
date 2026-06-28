#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_2.h>

#include "HWAccel.h"
#include <QDebug>

GPUInfo detectGPU()
{
    GPUInfo info;

    IDXGIFactory1 *factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        qWarning() << "HWAccel: CreateDXGIFactory1 failed" << hr;
        return info;
    }

    IDXGIAdapter1 *adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            desc.VendorId != 0 &&
            desc.VendorId != 0x1414)
        {
            info.vendorId = desc.VendorId;
            info.name = QString::fromWCharArray(desc.Description);
            adapter->Release();
            break;
        }
        adapter->Release();
    }

    factory->Release();

    qDebug() << "HWAccel: detected GPU" << info.name
             << "vendorId = 0x" + QString::number(info.vendorId, 16);

    return info;
}

std::vector<AVHWDeviceType> probeOrder(const GPUInfo &gpu)
{
    switch (gpu.vendorId) {
    case 0x10DE:
        return {AV_HWDEVICE_TYPE_DXVA2, AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_CUDA};
    case 0x8086:
        return {AV_HWDEVICE_TYPE_QSV, AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
    case 0x1002:
        return {AV_HWDEVICE_TYPE_AMF, AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
    default:
        return {AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
    }
}

QString backendName(DecodeBackend b)
{
    switch (b) {
    case DecodeBackend::NVDEC:     return "NVDEC";
    case DecodeBackend::QuickSync: return "QuickSync";
    case DecodeBackend::AMF:       return "AMF";
    case DecodeBackend::D3D11VA:   return "D3D11VA";
    default:                       return "Software";
    }
}

DecodeBackend typeToBackend(AVHWDeviceType type)
{
    switch (type) {
    case AV_HWDEVICE_TYPE_CUDA:    return DecodeBackend::NVDEC;
    case AV_HWDEVICE_TYPE_QSV:     return DecodeBackend::QuickSync;
    case AV_HWDEVICE_TYPE_AMF:     return DecodeBackend::AMF;
    case AV_HWDEVICE_TYPE_D3D11VA: return DecodeBackend::D3D11VA;
    default:                       return DecodeBackend::Software;
    }
}
