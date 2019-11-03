#include <windows.h>
#include <wrl.h>

#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

LRESULT CALLBACK WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );

bool Init( HWND hwnd );
bool InitPipeline( HWND hwnd );
bool InitResource();
bool Render();
bool PopulateCommandList();
bool WaitForPreviousFrame();
bool Destroy();

const UINT FRAME_COUNT = 2;

ComPtr<ID3D12Device> g_device;
ComPtr<ID3D12CommandQueue> g_commandQueue;
ComPtr<IDXGISwapChain3> g_swapChain;
ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
ComPtr<ID3D12Resource> g_renderTarget[FRAME_COUNT];
ComPtr<ID3D12CommandAllocator> g_commandAllocator;
ComPtr<ID3D12GraphicsCommandList> g_commandList;
ComPtr<ID3D12Fence> g_fence;
ComPtr<ID3D12PipelineState> g_pipelineState;

bool g_useWarpDevice = false;
UINT g_frameIndex = 0;
UINT g_rtvDescriptorSize = 0;
UINT g_fenceValue;
HANDLE g_fenceEvent;


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	//�E�B���h�E�̏�����-------------------------------
	WNDCLASSEX windowClass = {0};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = "DirectX12������";
	RegisterClassEx(&windowClass);

	RECT windowRect = {0,0,static_cast<LONG>(1280),static_cast<LONG>(720)};
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

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
		nullptr,
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
bool InitPipeline( HWND hwnd )
{
	UINT digixFactoryFlags = 0;

#if defined(_DEBUG)
	//�f�o�b�O���C���[��L���ɂ���
	{
		ComPtr<ID3D12Debug> debugController;
		if( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
		{
			debugController->EnableDebugLayer();

			//�f�o�b�O�p�̃t���O��ǉ�
			digixFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	//DirectX12���T�|�[�g���闘�p�\�ȃn�[�h�E�G�A�A�_�v�^���擾
	ComPtr<IDXGIFactory4> factory;
	if( FAILED( CreateDXGIFactory2( digixFactoryFlags,IID_PPV_ARGS(&factory) ) ) )
	{
		return false;
	}

	//�f�o�C�X�쐬
	if( g_useWarpDevice )
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		if( FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))) )
		{
			return false;
		}
		if( FAILED(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&g_device))))
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
		for(UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1( adapterIndex, &adapter ); adapterIndex++)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);

			if( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE )
			{
				continue;
			}

			if( SUCCEEDED( D3D12CreateDevice( adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr ) ) )
			{
				break;
			}

		}
		*ppAdapter = adapter.Detach();

		if( FAILED(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&g_device))))
		{
			return false;
		}
	}

	//�R�}���h�L���[�쐬
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	if( FAILED( g_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS(&g_commandQueue) ) ) )
	{
		return false;
	}


	//�X���b�v�`�F�C�����쐬
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Width = 1280;
	swapChainDesc.Height = 720;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	if( FAILED( factory->CreateSwapChainForHwnd(
		g_commandQueue.Get(),
		hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain ) ) )
	{
		return false;
	}

	//�t���X�N���[���T�|�[�g�Ȃ�
	if( FAILED( factory->MakeWindowAssociation( hwnd, DXGI_MWA_NO_ALT_ENTER ) ) )
	{
		return false;
	}

	if( FAILED( swapChain.As(&g_swapChain) ) )
	{
		return false;
	}
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	
	
	//�L�q�q�q�[�v�쐬
	{
		//�����_�[�^�[�Q�b�g�r���[�p�̋L�q�q�q�[�v�쐬
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = 2;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if( FAILED( g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap)) ) )
		{
			return false;
		}
		g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	//�t���[�����\�[�X�̍쐬
	{
		D3D12_CPU_DESCRIPTOR_HANDLE	rtvHandle = {};
		rtvHandle.ptr = g_rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr;

		//�e�t���[���̃����_�[�^�[�Q�b�g�r���[���쐬
		for(UINT n = 0;n < FRAME_COUNT;n++)
		{
			if( FAILED( g_swapChain->GetBuffer( n, IID_PPV_ARGS( &g_renderTarget[n] ) ) ) )
			{
				return false;
			}
			g_device->CreateRenderTargetView( g_renderTarget[n].Get(), nullptr, rtvHandle );
			rtvHandle.ptr += g_rtvDescriptorSize;

		}
	}
	
	//�R�}���h�A���P�[�^�[�쐬
	if( FAILED( g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocator)) ) )
	{
		return false;
	}

	return true;
}

//���\�[�X�̏�����
bool InitResource()
{
	//�R�}���h���X�g�쐬
	if( FAILED( g_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocator.Get(), nullptr, IID_PPV_ARGS( &g_commandList ) ) ) )
	{
		return false;
	}

	//�R�}���h���X�g�����
	if( FAILED( g_commandList->Close() ) )
	{
		return false;
	}

	//�����I�u�W�F�N�g���쐬���ă��\�[�X��GPU���A�b�v���[�h�����܂ő҂�
	{
		//�t�F���X�쐬
		if( FAILED( g_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence) ) ) )
		{
			return false;
		}
		g_fenceValue = 1;

		//�C�x���g�n���h���쐬
		g_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
		if(g_fenceEvent == nullptr)
		{
			if( FAILED( HRESULT_FROM_WIN32( GetLastError() ) ) )
			{
				return false;
			}
		}

		//�O�̃t���[����҂�
		if(!WaitForPreviousFrame())
		{
			return false;
		}
	}

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
		return -1;
	}

	//�O�̃t���[����҂�
	if(!WaitForPreviousFrame())
	{
		return false;
	}

	return true;
}

bool PopulateCommandList()
{
	//�R�}���h���X�g�̃A���P�[�^�[�����Z�b�g
	if(FAILED( g_commandAllocator->Reset() ))
	{
		return false;
	}

	//�R�}���h���X�g�����Z�b�g
	if( FAILED( g_commandList->Reset( g_commandAllocator.Get(), g_pipelineState.Get() ) ) )
	{
		return false;
	}

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

	D3D12_CPU_DESCRIPTOR_HANDLE	rtvHandle = {};
	rtvHandle.ptr = g_rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr+g_frameIndex*g_rtvDescriptorSize;
	g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	//�o�b�N�o�b�t�@�`��
	const float clearColor[] = { 1.0f, 0.0f, 0.0f, 1.0f };
	g_commandList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );

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

bool WaitForPreviousFrame()
{
	//�ȒP�Ɏ������邽�߂ɑ��s����O�Ƀt���[�����I���܂ő҂�
	const UINT64 fence = g_fenceValue;
	if( FAILED( g_commandQueue->Signal( g_fence.Get(), fence ) ) )
	{
		return false;
	}
	g_fenceValue++;
	if( g_fence->GetCompletedValue() < fence )
	{
		if( FAILED( g_fence->SetEventOnCompletion( fence, g_fenceEvent ) ))
		{
			return false;
		}
		WaitForSingleObject(g_fenceEvent,INFINITE);
	}
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	return true;
}

bool Destroy()
{
	//�O�̃t���[����҂�
	if(!WaitForPreviousFrame())
	{
		return false;
	}

	//�C�x���g�n���h�������
	CloseHandle(g_fenceEvent);
	
	return true;
}


