#include <windows.h>
#include <wrl.h>

#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include <DirectXMath.h>
#include <vector>
#include <fstream>

using namespace DirectX;
using Microsoft::WRL::ComPtr;
using namespace std;

LRESULT CALLBACK WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );

bool Init( HWND hwnd );
bool InitPipeline( HWND hwnd );
bool InitResource();
bool Update();
bool Render();
bool PopulateCommandList();
//bool WaitForPreviousFrame();
bool WaitForGpu();
bool MoveToNextFrame();
bool Destroy();


bool CreateDevice();
bool CreateCommandQueue();
bool CreateSwapChain(HWND hwnd);
bool CreateRenderTarget();
bool CreateDepthStencilBuffer();
bool CreateCommandList();

bool CreateRootSignature();
bool CompileShader();
bool CreatePipelineStateObject();
bool CreateVertexBuffer();
bool CreateCbvSrv();


std::vector<UINT8> LoadTexture( const char* fileName );

const UINT FRAME_COUNT = 2;

struct Vertex
{
	float position[3];
	float normal[3];
	float textureCoord[2];
};

struct Subset
{
	int mat_index;
	int vertexCount;
	int vertexStart;
};

__declspec(align(256))
struct Material
{
	float diffuse[3];
	float alpha;
	float ambient[3];
	float specular[3];
	float power;
	float emmisive[3];
};

struct Mesh
{
	int vertexCount;
	Vertex* vertecies;
	int indexCount;
	int* indexArray;
	int subsetCount;
	Subset* subset;
	int materialCount;
	Material* material;
	string* textureName;
};

__declspec(align(256))
struct ConstantBuffer
{
	XMMATRIX world;
	XMMATRIX view;
	XMMATRIX project;
};
__declspec(align(256))
struct LightBuffer
{
	XMFLOAT3 lightDirection;
};

//�p�C�v���C���I�u�W�F�N�g
ComPtr<ID3D12Device> g_device;
ComPtr<ID3D12CommandQueue> g_commandQueue;
ComPtr<IDXGISwapChain3> g_swapChain;
ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
ComPtr<ID3D12Resource> g_renderTarget[FRAME_COUNT];
ComPtr<ID3D12CommandAllocator> g_commandAllocator[FRAME_COUNT];
ComPtr<ID3D12GraphicsCommandList> g_commandList;
ComPtr<ID3D12PipelineState> g_pipelineState;
UINT g_rtvDescriptorSize = 0;
UINT g_dsvDescriptorSize = 0;
UINT g_cbvSrvDescriptorSize = 0;
ComPtr<ID3D12RootSignature> g_rootSignature;
D3D12_VIEWPORT g_viewport = { 0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f };
D3D12_RECT g_scissorRect = { 0, 0, 1280, 720 };
ComPtr<ID3D12DescriptorHeap> g_srvHeap;
ComPtr<ID3D12DescriptorHeap> g_cbvHeap;
ComPtr<ID3D12DescriptorHeap> g_cbvSrvHeap;

ComPtr<ID3D12Resource> g_depthStencil;
ComPtr<ID3D12DescriptorHeap> g_dsvHeap;

ComPtr<IDXGIFactory4> factory;


//���\�[�X
ComPtr<ID3D12Resource> g_vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView;
ComPtr<ID3D12Resource> g_indexBuffer;
D3D12_INDEX_BUFFER_VIEW g_indexBufferView;
ComPtr<ID3D12Resource> g_texture;
ComPtr<ID3D12Resource> g_constantBuffer = nullptr;
ComPtr<ID3D12Resource> g_lightBuffer = nullptr;
ComPtr<ID3D12Resource> g_materialBuffer = nullptr;
ConstantBuffer g_constantBufferData;
LightBuffer g_lightBufferData;
UINT8* g_pCbvDataBegin;
UINT8* g_pCbv2DataBegin;
UINT8* g_pCbv3DataBegin;

Mesh g_mesh;

//�����I�u�W�F�N�g
ComPtr<ID3D12Fence> g_fence;
UINT g_frameIndex = 0;
UINT64 g_fenceValue[FRAME_COUNT] = {};
HANDLE g_fenceEvent;

bool g_useWarpDevice = false;
float g_aspectRatio;

//------------------------------------------------------------------------------------------------
// Returns required size of a buffer to be used for data upload
inline UINT64 GetRequiredIntermediateSize(
	_In_ ID3D12Resource* pDestinationResource,
	_In_range_(0,D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
	_In_range_(0,D3D12_REQ_SUBRESOURCES-FirstSubresource) UINT NumSubresources)
{
	auto Desc = pDestinationResource->GetDesc();
	UINT64 RequiredSize = 0;

	ID3D12Device* pDevice = nullptr;
	pDestinationResource->GetDevice(__uuidof(*pDevice), reinterpret_cast<void**>(&pDevice));
	pDevice->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, 0, nullptr, nullptr, nullptr, &RequiredSize);
	pDevice->Release();

	return RequiredSize;
}

//------------------------------------------------------------------------------------------------
// Row-by-row memcpy
inline void MemcpySubresource(
	_In_ const D3D12_MEMCPY_DEST* pDest,
	_In_ const D3D12_SUBRESOURCE_DATA* pSrc,
	SIZE_T RowSizeInBytes,
	UINT NumRows,
	UINT NumSlices)
{
	for (UINT z = 0; z < NumSlices; ++z)
	{
		BYTE* pDestSlice = reinterpret_cast<BYTE*>(pDest->pData) + pDest->SlicePitch * z;
		const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(pSrc->pData) + pSrc->SlicePitch * z;
		for (UINT y = 0; y < NumRows; ++y)
		{
			memcpy(pDestSlice + pDest->RowPitch * y,
				pSrcSlice + pSrc->RowPitch * y,
				RowSizeInBytes);
		}
	}
}

//------------------------------------------------------------------------------------------------
// All arrays must be populated (e.g. by calling GetCopyableFootprints)
inline UINT64 UpdateSubresources(
	_In_ ID3D12GraphicsCommandList* pCmdList,
	_In_ ID3D12Resource* pDestinationResource,
	_In_ ID3D12Resource* pIntermediate,
	_In_range_(0,D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
	_In_range_(0,D3D12_REQ_SUBRESOURCES-FirstSubresource) UINT NumSubresources,
	UINT64 RequiredSize,
	_In_reads_(NumSubresources) const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts,
	_In_reads_(NumSubresources) const UINT* pNumRows,
	_In_reads_(NumSubresources) const UINT64* pRowSizesInBytes,
	_In_reads_(NumSubresources) const D3D12_SUBRESOURCE_DATA* pSrcData)
{
	// Minor validation
	auto IntermediateDesc = pIntermediate->GetDesc();
	auto DestinationDesc = pDestinationResource->GetDesc();
	if (IntermediateDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || 
		IntermediateDesc.Width < RequiredSize + pLayouts[0].Offset || 
		RequiredSize > SIZE_T(-1) || 
		(DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && 
		(FirstSubresource != 0 || NumSubresources != 1)))
	{
		return 0;
	}

	BYTE* pData;
	HRESULT hr = pIntermediate->Map(0, nullptr, reinterpret_cast<void**>(&pData));
	if (FAILED(hr))
	{
		return 0;
	}

	for (UINT i = 0; i < NumSubresources; ++i)
	{
		if (pRowSizesInBytes[i] > SIZE_T(-1)) return 0;
		D3D12_MEMCPY_DEST DestData = { pData + pLayouts[i].Offset, pLayouts[i].Footprint.RowPitch, SIZE_T(pLayouts[i].Footprint.RowPitch) * SIZE_T(pNumRows[i]) };
		MemcpySubresource(&DestData, &pSrcData[i], static_cast<SIZE_T>(pRowSizesInBytes[i]), pNumRows[i], pLayouts[i].Footprint.Depth);
	}
	pIntermediate->Unmap(0, nullptr);

	if (DestinationDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		pCmdList->CopyBufferRegion(
			pDestinationResource, 0, pIntermediate, pLayouts[0].Offset, pLayouts[0].Footprint.Width);
	}
	else
	{
		for (UINT i = 0; i < NumSubresources; ++i)
		{
			D3D12_TEXTURE_COPY_LOCATION Dst;
			Dst.pResource = pDestinationResource;
			Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			Dst.SubresourceIndex = i + FirstSubresource;

			D3D12_TEXTURE_COPY_LOCATION Src = {};
			Src.pResource = pIntermediate;
			Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			Src.PlacedFootprint = pLayouts[i];

			pCmdList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
		}
	}
	return RequiredSize;
}

//------------------------------------------------------------------------------------------------
// Heap-allocating UpdateSubresources implementation
UINT64 UpdateSubresources( 
	_In_ ID3D12GraphicsCommandList* pCmdList,
	_In_ ID3D12Resource* pDestinationResource,
	_In_ ID3D12Resource* pIntermediate,
	UINT64 IntermediateOffset,
	_In_range_(0,D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
	_In_range_(0,D3D12_REQ_SUBRESOURCES-FirstSubresource) UINT NumSubresources,
	_In_reads_(NumSubresources) D3D12_SUBRESOURCE_DATA* pSrcData)
{
	UINT64 RequiredSize = 0;
	UINT64 MemToAlloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * NumSubresources;
	if (MemToAlloc > SIZE_MAX)
	{
		return 0;
	}
	void* pMem = HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(MemToAlloc));
	if (pMem == nullptr)
	{
		return 0;
	}
	auto pLayouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
	UINT64* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + NumSubresources);
	UINT* pNumRows = reinterpret_cast<UINT*>(pRowSizesInBytes + NumSubresources);

	auto Desc = pDestinationResource->GetDesc();
	ID3D12Device* pDevice = nullptr;
	pDestinationResource->GetDevice(__uuidof(*pDevice), reinterpret_cast<void**>(&pDevice));
	pDevice->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);
	pDevice->Release();

	UINT64 Result = UpdateSubresources(pCmdList, pDestinationResource, pIntermediate, FirstSubresource, NumSubresources, RequiredSize, pLayouts, pNumRows, pRowSizesInBytes, pSrcData);
	HeapFree(GetProcessHeap(), 0, pMem);
	return Result;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	//�E�B���h�E�̏�����-------------------------------
	WNDCLASSEX windowClass = {0};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = "DirectX12�萔�o�b�t�@";
	RegisterClassEx(&windowClass);

	RECT windowRect = {0,0,static_cast<LONG>(1280),static_cast<LONG>(720)};
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	g_aspectRatio = static_cast<float>(1280) / static_cast<float>(720);

	HWND hwnd = CreateWindow( windowClass.lpszClassName,
		windowClass.lpszClassName,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		hInstance,
		nullptr
		);

	if(!Init(hwnd))
	{
		return -1;
	}

	ShowWindow(hwnd,SW_SHOW);
	UpdateWindow(hwnd);

	MSG msg;
	while(true)
	{
		if(PeekMessage(&msg,nullptr,0,0,PM_REMOVE))
		{
			if( msg.message == WM_QUIT )
			{
				break;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			if(!Update())
			{
				return -1;
			}
			if(!Render())
			{
				return -1;
			}
		}
	}

	if(!Destroy())
	{
		return -1;
	}

	return 0;
}

LRESULT CALLBACK WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
	switch(message)
	{
	case WM_DESTROY:
		//�I�����b�Z�[�W���M
		PostQuitMessage(0);

		return 0;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

//������
bool Init(HWND hwnd )
{
	//DirectX12�̏�����

	//�p�C�v���C���̏�����
	if(!InitPipeline( hwnd ))
	{
		return false;
	}
	
	//���\�[�X�̏�����
	if(!InitResource())
	{
		return false;
	}

	return true;
}

//�p�C�v���C���̏�����
bool InitPipeline(HWND hwnd)
{
	//�f�o�C�X�̍쐬
	if(!CreateDevice())
	{
		return false;
	}

	//�R�}���h�L���[�̍쐬
	if(!CreateCommandQueue())
	{
		return false;
	}
	
	//�X���b�v�`�F�C���̍쐬
	if(!CreateSwapChain(hwnd))
	{
		return false;
	}

	//�����_�[�^�[�Q�b�g�̍쐬
	if(!CreateRenderTarget())
	{
		return false;
	}

	//�[�x�X�e���V���r���[�̍쐬
	if(!CreateDepthStencilBuffer())
	{
		return false;
	}
	
	//�R�}���h���X�g�̍쐬
	if(!CreateCommandList())
	{
		return false;
	}

	return true;
}

//���\�[�X�̏�����
bool InitResource()
{
	if(!CreateRootSignature())
	{
		return false;
	}
	if(!CreatePipelineStateObject())
	{
		return false;
	}

	if(!CreateVertexBuffer())
	{
		return false;
	}

	if(!CreateCbvSrv())
	{
		return false;
	}

	//�O�̃t���[����҂�
	/*if(!WaitForGpu())
	{
		return false;
	}*/
	return true;
}

//�X�V
bool Update()
{
	static float angle = 0.0f;
	angle += 0.01f;
	//g_constantBufferData.world = XMMatrixIdentity();
	g_constantBufferData.world = XMMatrixRotationY(angle);
	g_constantBufferData.view = XMMatrixLookAtLH({0.0f,3.0f * cosf(angle),-5.0f,0.0f},{0.0f,0.0f,0.0f,0.0f},{0.0f,1.0f,0.0f,0.0f});
	g_constantBufferData.project = XMMatrixPerspectiveFovLH(0.78539816339744830961566084581988f,1280.0f/720.0f,1.0f,10000.0f);
	memcpy(g_pCbvDataBegin,&g_constantBufferData,sizeof(g_constantBufferData));
	g_lightBufferData.lightDirection = XMFLOAT3(0.0f,1.0f,1.0f);
	memcpy(g_pCbv2DataBegin,&g_lightBufferData,sizeof(g_lightBufferData));
	return true;
}

//�`��
bool Render()
{
	//�R�}���h���X�g�̓��e��p�ӂ���
	if(!PopulateCommandList())
	{
		return false;
	}

	//�R�}���h���X�g�����s
	ID3D12CommandList* ppCommandList[] = {g_commandList.Get()};
	g_commandQueue->ExecuteCommandLists(_countof(ppCommandList), ppCommandList);

	//�o�b�N�o�b�t�@��\��
	if( FAILED( g_swapChain->Present(1,0) ) )
	{
		return false;
	}

	//�O�̃t���[����҂�
	if(!MoveToNextFrame())
	{
		return false;
	}

	return true;
}

bool PopulateCommandList()
{
	//�R�}���h���X�g�̃A���P�[�^�[�����Z�b�g
	if(FAILED( g_commandAllocator[g_frameIndex]->Reset() ))
	{
		return false;
	}

	//�R�}���h���X�g�����Z�b�g
	if( FAILED( g_commandList->Reset( g_commandAllocator[g_frameIndex].Get(), g_pipelineState.Get() ) ) )
	{
		return false;
	}

	//�K�v�ȏ���ݒ�
	g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());

	ID3D12DescriptorHeap* ppHeap[] = {g_cbvSrvHeap.Get()};
	g_commandList->SetDescriptorHeaps( _countof(ppHeap), ppHeap );

	// �f�B�X�N���v�^�q�[�v�e�[�u����ݒ�.
	auto handleCBV = g_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
	auto handleSRV = g_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
	handleSRV.ptr += g_cbvSrvDescriptorSize * 3;
	g_commandList->SetGraphicsRootDescriptorTable( 0, handleCBV );
	handleCBV.ptr += g_cbvSrvDescriptorSize;
	g_commandList->SetGraphicsRootDescriptorTable( 1, handleCBV );
	handleCBV.ptr += g_cbvSrvDescriptorSize;
	g_commandList->SetGraphicsRootDescriptorTable( 3, handleCBV );
	//g_commandList->SetGraphicsRootDescriptorTable(1,g_cbvHeap->GetGPUDescriptorHandleForHeapStart());
	//g_commandList->SetGraphicsRootDescriptorTable(1,g_srvHeap->GetGPUDescriptorHandleForHeapStart());
	
	g_commandList->RSSetViewports(1, &g_viewport);
	g_commandList->RSSetScissorRects( 1, &g_scissorRect );

	//���\�[�X�o���A��ݒ肵�ăo�b�N�o�b�t�@�������_�[�^�[�Q�b�g�Ƃ��Ďw��
	{
		D3D12_RESOURCE_BARRIER resourceBarrier = {};
		resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resourceBarrier.Transition.pResource = g_renderTarget[g_frameIndex].Get();
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		g_commandList->ResourceBarrier( 1, &resourceBarrier );
	}

	auto handleRTV = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	auto handleDSV = g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	handleRTV.ptr += ( g_frameIndex * g_rtvDescriptorSize );

	g_commandList->OMSetRenderTargets(1, &handleRTV, FALSE, &handleDSV);

	//�����_�[�^�[�Q�b�g�r���[���N���A
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	g_commandList->ClearRenderTargetView( handleRTV, clearColor, 0, nullptr );
	
	//�[�x�X�e���V���r���[���N���A
	g_commandList->ClearDepthStencilView(handleDSV,D3D12_CLEAR_FLAG_DEPTH,1.0f,0,0,nullptr);

	
	
	g_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
	g_commandList->IASetIndexBuffer(&g_indexBufferView);
	//g_commandList->DrawInstanced(g_mesh.vertexCount, 1, 0, 0);
	for(int i = 0;i < g_mesh.subsetCount;i++)
	{
		g_commandList->SetGraphicsRootDescriptorTable( 2, handleCBV );
		handleCBV.ptr += g_cbvSrvDescriptorSize;
		g_commandList->DrawIndexedInstanced(g_mesh.subset[i].vertexCount, 1, g_mesh.subset[i].vertexStart, 0, 0);
	}

	//�o�b�N�o�b�t�@��\��
	{
		D3D12_RESOURCE_BARRIER resourceBarrier = {};
		resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resourceBarrier.Transition.pResource = g_renderTarget[g_frameIndex].Get();
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		g_commandList->ResourceBarrier( 1, &resourceBarrier );
	}

	//�R�}���h���X�g�����
	if( FAILED( g_commandList->Close() ) )
	{
		return false;
	}

	return true;
}

//bool WaitForPreviousFrame()
//{
//	//�ȒP�Ɏ������邽�߂ɑ��s����O�Ƀt���[�����I���܂ő҂�
//	const UINT64 fence = g_fenceValue;
//	if( FAILED( g_commandQueue->Signal( g_fence.Get(), fence ) ) )
//	{
//		return false;
//	}
//	g_fenceValue++;
//	if( g_fence->GetCompletedValue() < fence )
//	{
//		if( FAILED( g_fence->SetEventOnCompletion( fence, g_fenceEvent ) ))
//		{
//			return false;
//		}
//		WaitForSingleObject(g_fenceEvent,INFINITE);
//	}
//	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
//
//	return true;
//}

bool WaitForGpu()
{
	if( FAILED( g_commandQueue->Signal(g_fence.Get(),g_fenceValue[g_frameIndex]) ) )
	{
		return false;
	}

	if( FAILED( g_fence->SetEventOnCompletion(g_fenceValue[g_frameIndex],g_fenceEvent) ) )
	{
		return false;
	}

	WaitForSingleObjectEx(g_fenceEvent,INFINITE,FALSE);

	g_fenceValue[g_frameIndex]++;

	return true;
}

bool MoveToNextFrame()
{
	const UINT64 fence = g_fenceValue[g_frameIndex];
	if( FAILED( g_commandQueue->Signal( g_fence.Get(), fence ) ) )
	{
		return false;
	}
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	if( g_fence->GetCompletedValue() < g_fenceValue[g_frameIndex] )
	{
		if( FAILED( g_fence->SetEventOnCompletion( g_fenceValue[g_frameIndex], g_fenceEvent ) ))
		{
			return false;
		}
		WaitForSingleObjectEx(g_fenceEvent,INFINITE,FALSE);
	}
	g_fenceValue[g_frameIndex] = fence + 1;
	
	return true;
}

bool Destroy()
{
	//�O�̃t���[����҂�
	if(!WaitForGpu())
	{
		return false;
	}

	//�C�x���g�n���h�������
	CloseHandle(g_fenceEvent);
	
	return true;
}

//�f�o�C�X�̍쐬
bool CreateDevice()
{
	UINT digixFactoryFlags = 0;

#if defined(_DEBUG)
	//�f�o�b�O���C���[��L���ɂ���
	{
		ComPtr<ID3D12Debug> debugController;
		if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			//�f�o�b�O�p�̃t���O��ǉ�
			digixFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	//DirectX12���T�|�[�g���闘�p�\�ȃn�[�h�E�G�A�A�_�v�^���擾
	if(FAILED(CreateDXGIFactory2(digixFactoryFlags,IID_PPV_ARGS(&factory))))
	{
		return false;
	}

	//�f�o�C�X�쐬
	if( g_useWarpDevice )
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		if(FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))))
		{
			return false;
		}
		if(FAILED(D3D12CreateDevice(warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&g_device))))
		{
			return false;
		}
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		ComPtr<IDXGIAdapter1> adapter;
		IDXGIAdapter1** ppAdapter = &hardwareAdapter;
		*ppAdapter = nullptr;
		IDXGIFactory2* pFactory = factory.Get();
		for(UINT adapterIndex = 0;DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex,&adapter );adapterIndex++)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				continue;
			}

			if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),
				D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),nullptr)))
			{
				break;
			}

		}
		*ppAdapter = adapter.Detach();

		if(FAILED(D3D12CreateDevice(hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&g_device))))
		{
			return false;
		}
	}

	return true;
}

//�R�}���h�L���[�̍쐬
bool CreateCommandQueue()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	if(FAILED(g_device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&g_commandQueue))))
	{
		return false;
	}

	//�t�F���X�쐬
	if(FAILED(g_device->CreateFence(g_fenceValue[g_frameIndex],D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&g_fence))))
	{
		return false;
	}
	g_fenceValue[g_frameIndex]++;

	//�t�F���X�p�̃C�x���g�n���h���쐬
	g_fenceEvent = CreateEventEx(nullptr,FALSE,FALSE,EVENT_ALL_ACCESS);
	if(g_fenceEvent == nullptr)
	{
		if(FAILED(HRESULT_FROM_WIN32(GetLastError())))
		{
			return false;
		}
	}

	return true;
}

//�X���b�v�`�F�C���̍쐬
bool CreateSwapChain(HWND hwnd)
{
	//�X���b�v�`�F�C�����쐬
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FRAME_COUNT;
	swapChainDesc.Width = 1280;
	swapChainDesc.Height = 720;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	if(FAILED(factory->CreateSwapChainForHwnd(
		g_commandQueue.Get(),hwnd,&swapChainDesc,
		nullptr,nullptr,&swapChain)))
	{
		return false;
	}

	//�t���X�N���[���T�|�[�g�Ȃ�
	if(FAILED(factory->MakeWindowAssociation(
		hwnd,DXGI_MWA_NO_ALT_ENTER)))
	{
		return false;
	}

	if(FAILED(swapChain.As(&g_swapChain)))
	{
		return false;
	}
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	return true;
}

//�����_�[�^�[�Q�b�g�̍쐬
bool CreateRenderTarget()
{
	//�����_�[�^�[�Q�b�g�r���[�p�̋L�q�q�q�[�v�쐬
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FRAME_COUNT;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if(FAILED(g_device->CreateDescriptorHeap(&rtvHeapDesc,IID_PPV_ARGS(&g_rtvHeap))))
	{
		return false;
	}

	//�����_�[�^�[�Q�b�g�r���[�̋L�q�q�T�C�Y���擾
	g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	//�e�t���[���̃����_�[�^�[�Q�b�g�r���[���쐬
	for(UINT n = 0;n < FRAME_COUNT;n++)
	{
		if(FAILED(g_swapChain->GetBuffer(n,IID_PPV_ARGS(&g_renderTarget[n]))))
		{
			return false;
		}
		g_device->CreateRenderTargetView(g_renderTarget[n].Get(),nullptr,rtvHandle);
		rtvHandle.ptr += g_rtvDescriptorSize;
	}

	return true;
}

//�[�x�X�e���V���r���[�̍쐬
bool CreateDepthStencilBuffer()
{
	//�[�x�X�e���V���r���[�p�̋L�q�q�q�[�v�쐬
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = FRAME_COUNT;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if(FAILED(g_device->CreateDescriptorHeap(&dsvHeapDesc,IID_PPV_ARGS(&g_dsvHeap))))
	{
		return false;
	}

	//�[�x�X�e���V���r���[�r���[�̋L�q�q�T�C�Y���擾
	g_dsvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	//�q�[�v�v���p�e�B�̐ݒ�
	D3D12_HEAP_PROPERTIES heapProperties;
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProperties.CreationNodeMask = 1;
	heapProperties.VisibleNodeMask = 1;

	//���\�[�X�̐ݒ�
	D3D12_RESOURCE_DESC resourceDesc;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Alignment = 0;
	resourceDesc.Width = 1280;
	resourceDesc.Height = 720;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 0;
	resourceDesc.Format = DXGI_FORMAT_D32_FLOAT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	//�N���A�l�̐ݒ�
	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	//���\�[�X�̍쐬
	if(FAILED(g_device->CreateCommittedResource(&heapProperties,
		D3D12_HEAP_FLAG_NONE,&resourceDesc,D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,IID_PPV_ARGS(&g_depthStencil))))
	{
		return false;
	}

	//�[�x�X�e���V���r���[�̍쐬
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	g_device->CreateDepthStencilView(g_depthStencil.Get(),&dsvDesc,g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

	return true;
}

//�R�}���h�L���[�E�R�}���h�A���P�[�^�̍쐬
bool CreateCommandList()
{
	for(UINT n = 0;n < FRAME_COUNT;n++)
	{
		//�R�}���h�A���P�[�^�[�쐬
		if(FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&g_commandAllocator[n]))))
		{
			return false;
		}
	}

	//�R�}���h���X�g�쐬
	if( FAILED( g_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocator[g_frameIndex].Get(), nullptr, IID_PPV_ARGS( &g_commandList ) ) ) )
	{
		return false;
	}

	//�R�}���h���X�g�����
	/*if( FAILED( g_commandList->Close() ) )
	{
		return false;
	}*/

	return true;
}

//���[�g�V�O�l�`���̍쐬
bool CreateRootSignature()
{
	//�L�q�q�����W�̐ݒ�
	D3D12_DESCRIPTOR_RANGE range[4];
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	range[0].NumDescriptors = 1;
	range[0].BaseShaderRegister = 0;
	range[0].RegisterSpace = 0;
	range[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	range[1].NumDescriptors = 1;
	range[1].BaseShaderRegister = 1;
	range[1].RegisterSpace = 0;
	range[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	range[2].NumDescriptors = 1;
	range[2].BaseShaderRegister = 2;
	range[2].RegisterSpace = 0;
	range[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	range[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range[3].NumDescriptors = 1;
	range[3].BaseShaderRegister = 0;
	range[3].RegisterSpace = 0;
	range[3].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;


	//���[�g�p�����[�^�̐ݒ�
	D3D12_ROOT_PARAMETER param[4];
	param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	param[0].DescriptorTable.NumDescriptorRanges = 1;
	param[0].DescriptorTable.pDescriptorRanges = &range[0];

	param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	param[1].DescriptorTable.NumDescriptorRanges = 1;
	param[1].DescriptorTable.pDescriptorRanges = &range[1];

	param[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	param[2].DescriptorTable.NumDescriptorRanges = 1;
	param[2].DescriptorTable.pDescriptorRanges = &range[2];

	param[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	param[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	param[3].DescriptorTable.NumDescriptorRanges = 1;
	param[3].DescriptorTable.pDescriptorRanges = &range[3];

	//�T���v���[�̐ݒ�
	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD= 0;
	sampler.MaxLOD= 0;
	sampler.ShaderRegister= 0;
	sampler.RegisterSpace= 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	//���[�g�V�O�l�`���̐ݒ�
	D3D12_ROOT_SIGNATURE_DESC desc;
	desc.NumParameters = _countof(param);
	desc.pParameters = param;
	desc.NumStaticSamplers = 1;
	desc.pStaticSamplers = &sampler;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;


	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;

	//�V���A���C�Y
	if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error)))
	{
		return false;
	}

	//���[�g�V�O�l�`���̍쐬
	if(FAILED(g_device->CreateRootSignature(0,signature->GetBufferPointer(),
		signature->GetBufferSize(),IID_PPV_ARGS(&g_rootSignature))))
	{
		return false;
	}

	return true;
}

//�V�F�[�_�[�̃R���p�C��
bool CompileShader()
{
	return true;

	
}

//�p�C�v���C���̏�ԃI�u�W�F�N�g���쐬
bool CreatePipelineStateObject()
{
	//�V�F�[�_�[�̃R���p�C��
	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
	UINT compileFlag = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;;
#else
	UINT compileFlag = 0;
#endif
	if( FAILED(D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlag, 0, &vertexShader, nullptr) ) )
	{
		return false;
	}
	if( FAILED(D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlag, 0, &pixelShader, nullptr) ) )
	{
		return false;
	}

	//���_���̓��C���[���`
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	//���X�^���C�U�[�X�e�[�g�̐ݒ�
	D3D12_RASTERIZER_DESC rasterrizerStateDesc;
	rasterrizerStateDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterrizerStateDesc.CullMode = D3D12_CULL_MODE_BACK;
	rasterrizerStateDesc.FrontCounterClockwise = FALSE;
	rasterrizerStateDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	rasterrizerStateDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	rasterrizerStateDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	rasterrizerStateDesc.DepthClipEnable = TRUE;
	rasterrizerStateDesc.MultisampleEnable = FALSE;
	rasterrizerStateDesc.AntialiasedLineEnable = FALSE;
	rasterrizerStateDesc.ForcedSampleCount = 0;
	rasterrizerStateDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	//�����_�[�^�[�Q�b�g�̃u�����h�ݒ�
	D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlendStateDesc =
	{
		FALSE,FALSE,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_LOGIC_OP_NOOP,
		D3D12_COLOR_WRITE_ENABLE_ALL
	};

	//�u�����h�X�e�[�g�̐ݒ�
	D3D12_BLEND_DESC blendStateDesc;
	blendStateDesc.AlphaToCoverageEnable = FALSE;
	blendStateDesc.IndependentBlendEnable = FALSE;
	for(UINT i = 0;i<D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;i++)
	{
		blendStateDesc.RenderTarget[i] = renderTargetBlendStateDesc;
	}
	//�O���t�B�b�N�X�p�C�v���C���̏�ԃI�u�W�F�N�g���쐬
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	psoDesc.pRootSignature = g_rootSignature.Get();
	psoDesc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
	psoDesc.VS.BytecodeLength = vertexShader->GetBufferSize();
	psoDesc.PS.pShaderBytecode = pixelShader->GetBufferPointer();
	psoDesc.PS.BytecodeLength = pixelShader->GetBufferSize();
	psoDesc.RasterizerState = rasterrizerStateDesc;
	psoDesc.BlendState = blendStateDesc;
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count = 1;
	if(FAILED(g_device->CreateGraphicsPipelineState(&psoDesc,IID_PPV_ARGS(&g_pipelineState))))
	{
		return false;
	}

	return true;
}

//���_�o�b�t�@�̍쐬
bool CreateVertexBuffer()
{
	//�O�p�`�̃W�I���g�����`
	/*Vertex vertices[] =
	{
		{ {1.0f, 1.0f, 1.0f}, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ {1.0f, -1.0f, 1.0f}, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },
		{ {-1.0f, -1.0f, 1.0f}, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },

		{ {1.0f, 1.0f, 1.0f}, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ {-1.0f, 1.0f, 1.0f}, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
		{ {-1.0f, -1.0f, 1.0f}, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },

		{ {1.0f, 1.0f, -1.0f}, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
		{ {1.0f, -1.0f, -1.0f}, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
		{ {-1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },

		{ {1.0f, 1.0f, -1.0f}, { 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
		{ {-1.0f, 1.0f, -1.0f}, { 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
		{ {-1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },


		{ {1.0f, 1.0f, 1.0f}, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ {1.0f, 1.0f, -1.0f}, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
		{ {-1.0f, 1.0f, -1.0f}, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },

		{ {1.0f, 1.0f, 1.0f}, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ {-1.0f, 1.0f, 1.0f}, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ {-1.0f, 1.0f, -1.0f}, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },

		{ {1.0f, -1.0f, 1.0f}, { 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
		{ {1.0f, -1.0f, -1.0f}, { 1.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } },
		{ {-1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } },

		{ {1.0f, -1.0f, 1.0f}, { 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
		{ {-1.0f, -1.0f, 1.0f}, { 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } },
		{ {-1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f } },


		{ {1.0f, 1.0f, 1.0f}, { 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ {1.0f, 1.0f, -1.0f}, { 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
		{ {1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },

		{ {1.0f, 1.0f, 1.0f}, { 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ {1.0f, -1.0f, 1.0f}, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
		{ {1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },

		{ {-1.0f, 1.0f, 1.0f}, { 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
		{ {-1.0f, 1.0f, -1.0f}, { 1.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },
		{ {-1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },

		{ {-1.0f, 1.0f, 1.0f}, { 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
		{ {-1.0f, -1.0f, 1.0f}, { 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
		{ {-1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f }, { -1.0f, 0.0f, 0.0f } },
	};*/


	//ifstream file("boxMaterial.gmb",ios::binary);
	//ifstream file("SD_unitychan_humanoid.gmb",ios::binary);
	ifstream file("boxMaterial.gmb",ios::binary);

	if(!file.is_open())
	{
		return false;
	}

	file.read(reinterpret_cast<char*>(&g_mesh.vertexCount),sizeof(int));
	file.read(reinterpret_cast<char*>(&g_mesh.indexCount),sizeof(int));
	file.read(reinterpret_cast<char*>(&g_mesh.subsetCount),sizeof(int));
	file.read(reinterpret_cast<char*>(&g_mesh.materialCount),sizeof(int));

	g_mesh.vertecies = new Vertex[g_mesh.vertexCount];
	g_mesh.indexArray = new int[g_mesh.indexCount];
	g_mesh.subset = new Subset[g_mesh.subsetCount];
	

	file.read(reinterpret_cast<char*>(g_mesh.vertecies),sizeof(Vertex) * g_mesh.vertexCount);
	file.read(reinterpret_cast<char*>(g_mesh.indexArray),sizeof(int) * g_mesh.indexCount);
	file.read(reinterpret_cast<char*>(g_mesh.subset),sizeof(Subset) * g_mesh.subsetCount);

	g_mesh.material = nullptr;
	if(g_mesh.materialCount > 0)
	{
		g_mesh.material = new Material[g_mesh.materialCount];
		g_mesh.textureName = new string[g_mesh.materialCount];
		for(int i = 0;i < g_mesh.materialCount;i++)
		{
			file.read(reinterpret_cast<char*>(g_mesh.material[i].diffuse),sizeof(float) * 3);
			file.read(reinterpret_cast<char*>(&g_mesh.material[i].alpha),sizeof(float));
			file.read(reinterpret_cast<char*>(g_mesh.material[i].ambient),sizeof(float) * 3);
			file.read(reinterpret_cast<char*>(g_mesh.material[i].specular),sizeof(float) * 3);
			file.read(reinterpret_cast<char*>(&g_mesh.material[i].power),sizeof(float));
			file.read(reinterpret_cast<char*>(g_mesh.material[i].emmisive),sizeof(float) * 3);


			int nameLength;
			file.read(reinterpret_cast<char*>(&nameLength),sizeof(int));

			char* textureName = new char[nameLength];
			file.read(textureName,nameLength);
			g_mesh.textureName[i] = textureName;

			//Todo:�e�N�X�`���ǂݍ��݁H
		}
	}

	file.close();

	const UINT vertexBufferSize = sizeof(Vertex) * g_mesh.vertexCount;

	//�q�[�v�v���p�e�B�̐ݒ�
	D3D12_HEAP_PROPERTIES heapProperties = {};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProperties.CreationNodeMask = 1;
	heapProperties.VisibleNodeMask = 1;

	//���\�[�X�̐ݒ�
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Alignment = 0;
	resourceDesc.Width = vertexBufferSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	if(FAILED(g_device->CreateCommittedResource(&heapProperties,
		D3D12_HEAP_FLAG_NONE,&resourceDesc,D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,IID_PPV_ARGS(&g_vertexBuffer))))
	{
		return false;
	}

	//�}�b�v
	UINT8* pVertexDataBegin;
	D3D12_RANGE readRange = {0,0};
	if(FAILED(g_vertexBuffer->Map(0,&readRange,
		reinterpret_cast<void**>(&pVertexDataBegin))))
	{
		return false;
	}
	//���_�f�[�^���R�s�[
	memcpy(pVertexDataBegin,g_mesh.vertecies, vertexBufferSize );
	//�A���}�b�v
	g_vertexBuffer->Unmap( 0, nullptr );

	//���_�o�b�t�@�r���[�̐ݒ�
	g_vertexBufferView.BufferLocation = g_vertexBuffer->GetGPUVirtualAddress();
	g_vertexBufferView.StrideInBytes = sizeof(Vertex);
	g_vertexBufferView.SizeInBytes = vertexBufferSize;



	const UINT indexBufferSize = sizeof(int) * g_mesh.indexCount;

	if(FAILED(g_device->CreateCommittedResource(&heapProperties,
		D3D12_HEAP_FLAG_NONE,&resourceDesc,D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,IID_PPV_ARGS(&g_indexBuffer))))
	{
		return false;
	}

	//�}�b�v
	UINT8* pIndexDataBegin;
	if(FAILED(g_indexBuffer->Map(0,&readRange,
		reinterpret_cast<void**>(&pIndexDataBegin))))
	{
		return false;
	}
	//���_�f�[�^���R�s�[
	memcpy(pIndexDataBegin,g_mesh.indexArray,indexBufferSize);
	//�A���}�b�v
	g_indexBuffer->Unmap( 0, nullptr );

	//���_�o�b�t�@�r���[�̐ݒ�
	g_indexBufferView.BufferLocation = g_indexBuffer->GetGPUVirtualAddress();
	g_indexBufferView.SizeInBytes = indexBufferSize;
	g_indexBufferView.Format = DXGI_FORMAT_R32_UINT;

	return true;
}

bool CreateCbvSrv()
{
	//�萔�o�b�t�@�A�V�F�[�_�[���\�[�X�r���[�p�̋L�q�q�q�[�v�쐬
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = 3 + g_mesh.materialCount;	//�萔�o�b�t�@�ƃV�F�[�_���\�[�X�r���[���쐬����̂�2��
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		if(FAILED(g_device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&g_cbvSrvHeap))))
		{
			return false;
		}
	}
	//�萔�o�b�t�@���쐬
	{
		//�q�[�v�v���p�e�B�̐ݒ�
		D3D12_HEAP_PROPERTIES prop = {};
		prop.Type = D3D12_HEAP_TYPE_UPLOAD;
		prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		prop.CreationNodeMask = 1;
		prop.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = sizeof(ConstantBuffer);
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		//���\�[�X�쐬
		if(FAILED(g_device->CreateCommittedResource(
			&prop,D3D12_HEAP_FLAG_NONE,&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,IID_PPV_ARGS(&g_constantBuffer))))
		{
			return false;
		}

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = g_constantBuffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = sizeof(ConstantBuffer);

		//�萔�o�b�t�@�r���[�쐬
		D3D12_CPU_DESCRIPTOR_HANDLE handle = g_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart();
		g_device->CreateConstantBufferView(&cbvDesc,handle);
		g_cbvSrvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		handle.ptr += g_cbvSrvDescriptorSize;

		D3D12_RANGE readRange = {0,0};
		if( FAILED(g_constantBuffer->Map(0,&readRange,reinterpret_cast<void**>(&g_pCbvDataBegin) )))
		{
			return false;
		}
		//XMMATRIX proj = XMMatrixOrthographicLH(1280.0f,720.0f,10.0f,100000.0f);
		g_constantBufferData.world = XMMatrixIdentity();
		g_constantBufferData.view = XMMatrixLookAtLH({0.0f,0.0f,-0.5f,0.0f},{0.0f,0.0f,0.0f,0.0f},{0.0f,1.0f,0.0f,0.0f});
		g_constantBufferData.project = XMMatrixPerspectiveFovLH(0.78539816339744830961566084581988f,1280.0f/720.0f,1.0f,1000.0f);
		memcpy(g_pCbvDataBegin,&g_constantBufferData,sizeof(g_constantBufferData));


		// ���C�g
		desc.Width = sizeof(LightBuffer);

		//���\�[�X�쐬
		if(FAILED(g_device->CreateCommittedResource(
			&prop,D3D12_HEAP_FLAG_NONE,&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,IID_PPV_ARGS(&g_lightBuffer))))
		{
			return false;
		}

		cbvDesc.BufferLocation = g_lightBuffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = sizeof(LightBuffer);

		//�萔�o�b�t�@�r���[�쐬
		g_device->CreateConstantBufferView(&cbvDesc,handle);
		handle.ptr += g_cbvSrvDescriptorSize;

		readRange = {0,0};
		if( FAILED(g_lightBuffer->Map(0,&readRange,reinterpret_cast<void**>(&g_pCbv2DataBegin) )))
		{
			return false;
		}
		g_lightBufferData.lightDirection = XMFLOAT3(0.0f,1.0f,1.0f);
		memcpy(g_pCbv2DataBegin,&g_lightBufferData,sizeof(g_lightBufferData));

		// ���C�g
		desc.Width = sizeof(Material) * g_mesh.materialCount;

		//���\�[�X�쐬
		if(FAILED(g_device->CreateCommittedResource(
			&prop,D3D12_HEAP_FLAG_NONE,&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,IID_PPV_ARGS(&g_materialBuffer))))
		{
			return false;
		}

		if( FAILED(g_materialBuffer->Map(0,nullptr,reinterpret_cast<void**>(&g_pCbv3DataBegin) )))
		{
			return false;
		}

		memcpy(g_pCbv3DataBegin,&g_mesh.material[0],sizeof(Material) * g_mesh.materialCount);

		cbvDesc.BufferLocation = g_materialBuffer->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = sizeof(Material);

		for(int i = 0;i < g_mesh.materialCount;i++)
		{
			//�萔�o�b�t�@�r���[�쐬
			g_device->CreateConstantBufferView(&cbvDesc,handle);
			handle.ptr += g_cbvSrvDescriptorSize;
			cbvDesc.BufferLocation += sizeof(Material);
		}

		
	}

	//�V�F�[�_�[���\�[�X�r���[�̍쐬
	ComPtr<ID3D12Resource> textureUploadHeap;
	{
		std::vector<UINT8> texture = LoadTexture( "boxtexture.bmp" );

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = 256;
		desc.Height = 256;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		
		

		D3D12_HEAP_PROPERTIES prop = {};
		prop.Type = D3D12_HEAP_TYPE_DEFAULT;
		prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		prop.CreationNodeMask = 1;
		prop.VisibleNodeMask = 1;

		if(FAILED(g_device->CreateCommittedResource(&prop,
			D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,IID_PPV_ARGS(&g_texture))))
		{
			return false;
		}

		//�T�u���\�[�X�̍X�V
		/*if(!UpdateSubresouce(g_commandList.Get(),g_commandQueue.Get(),
			g_fence.Get(),g_fenceEvent,g_texture.Get(),0,1,&subResourceData))
		{
			return false;
		}*/

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(g_texture.Get(), 0, 1);

		D3D12_HEAP_PROPERTIES props = {};
		props.Type = D3D12_HEAP_TYPE_UPLOAD;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		props.CreationNodeMask = 1;
		props.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC uploadDesc = {};
		uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadDesc.Alignment = 0;
		uploadDesc.Width = uploadBufferSize;
		uploadDesc.Height = 1;
		uploadDesc.DepthOrArraySize = 1;
		uploadDesc.MipLevels = 1;
		uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
		uploadDesc.SampleDesc.Count = 1;
		uploadDesc.SampleDesc.Quality = 0;
		uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;


		// Create the GPU upload buffer.
		if(FAILED(g_device->CreateCommittedResource(
			&props,
			D3D12_HEAP_FLAG_NONE,
			&uploadDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&textureUploadHeap))))
		{
			return false;
		}

		//�T�u���\�[�X�f�[�^�̐ݒ�
		D3D12_SUBRESOURCE_DATA subResourceData;
		subResourceData.pData = &texture[0];
		subResourceData.RowPitch = 256 * 4;
		subResourceData.SlicePitch = subResourceData.RowPitch * 256;

		UpdateSubresources(g_commandList.Get(),
			g_texture.Get(),
			textureUploadHeap.Get(), 0, 0, 1, &subResourceData);

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = g_texture.Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;


		g_commandList->ResourceBarrier(1, &barrier);

		//�V�F�[�_�[���\�[�X�r���[�̐ݒ�
		D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
		viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		viewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		viewDesc.Texture2D.MipLevels = 1;
		viewDesc.Texture2D.MostDetailedMip = 0;

		//�V�F�[�_�[���\�[�X�r���[�̍쐬
		D3D12_CPU_DESCRIPTOR_HANDLE handle = g_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += g_cbvSrvDescriptorSize * 5;
		g_device->CreateShaderResourceView(g_texture.Get(),&viewDesc,handle);

		

		////�e�N�X�`���p�̃V�F�[�_�[���\�[�X�r���[���쐬
		//D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle = {};
		//D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
		//shaderResourceViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		//shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		//shaderResourceViewDesc.Texture2D.MipLevels = 1;
		//shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
		//shaderResourceViewDesc.Texture2D.PlaneSlice = 0;
		//shaderResourceViewDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		//shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		//g_device->CreateShaderResourceView(g_texture.Get(), &shaderResourceViewDesc, g_srvHeap->GetCPUDescriptorHandleForHeapStart());

		//D3D12_BOX box = {0,0,0,(UINT)256,(UINT)256,1};
		//if(FAILED(g_texture->WriteToSubresource(0,&box,&texture[0],4*256,4*256*256)))
		//{
		//	return false;
		//}
	}

	// Close the command list and execute it to begin the initial GPU setup.
	if(FAILED(g_commandList->Close()))
	{
		return false;
	}
	ID3D12CommandList* ppCommandLists[] = { g_commandList.Get() };
	g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		if(FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
		{
			return false;
		}
		g_fenceValue[g_frameIndex] = 1;

		// Create an event handle to use for frame synchronization.
		g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (g_fenceEvent == nullptr)
		{
			if(FAILED(HRESULT_FROM_WIN32(GetLastError())))
			{
				return false;
			}
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		//WaitForPreviousFrame();
		WaitForGpu();
	}

	return true;
}


std::vector<UINT8> LoadTexture( const char* fileName )
{
	ifstream ifile( fileName, ios::binary );

	vector<UINT8> data(256*256 * 4);

	if( !ifile.is_open() )
	{
		return data;
	}
#pragma pack(1)
	struct BitmapHeader
	{
		unsigned char type[2];
		unsigned int size;
		unsigned short reserved[2];
		unsigned int offset;
	};
#pragma pack()

	struct BitmapInfoHeader
	{
		unsigned int size;
		int width;
		int height;
		unsigned short planes;
		unsigned short bitCount;
		unsigned int compression;
		unsigned int sizeImage;
		int XPixPerMeter;
		int YPixPerMeter;
		unsigned int ClrUsed;
		unsigned int CirImportant;
	};
	struct BitmapColor24
	{
		unsigned char b;
		unsigned char g;
		unsigned char r;
	};

	struct BitmapColor32
	{
		unsigned char b;
		unsigned char g;
		unsigned char r;
		unsigned char a;
	};

	BitmapHeader header;
	BitmapInfoHeader infoHeader;

	ifile.read(reinterpret_cast<char*>(&header),sizeof(header));
	ifile.read(reinterpret_cast<char*>(&infoHeader),sizeof(infoHeader));
	
	int colorSize = infoHeader.width * infoHeader.height;
	if(infoHeader.bitCount == 24)
	{
		BitmapColor24* color = new BitmapColor24[colorSize];
		ifile.read(reinterpret_cast<char*>(color),sizeof(BitmapColor24) * colorSize);
		BitmapColor32* color32 = new BitmapColor32[colorSize];
		for(int i = 0;i < infoHeader.height / 2;i++)
		{
			for(int k = 0;k < infoHeader.width;k++)
			{
				int index1 = k + i * infoHeader.width;
				int index2 = k + (infoHeader.height-1-i) * infoHeader.width;
				BitmapColor24 tmp = color[index1];
				color[index1] = color[index2];
				color[index2] = tmp;
			}
		}
		for(int i = 0;i < colorSize;i++)
		{
			color32[i].r = color[i].b;
			color32[i].g = color[i].g;
			color32[i].b = color[i].r;
			color32[i].a = 255;
		}

		memcpy(&data[0],color32,sizeof(BitmapColor32) * colorSize);

		delete[] color;
		delete[] color32;
	}
	else if(infoHeader.bitCount == 32)
	{
		ifile.read(reinterpret_cast<char*>(&data[0]),sizeof(BitmapColor24) * colorSize);
	}

	ifile.close();

	return data;
}

