#include <windows.h>
#include <wrl.h>
#include <cstdlib>

#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

LRESULT CALLBACK WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	//ウィンドウの初期化-------------------------------
	WNDCLASSEX windowClass = {0};
	windowClass.cbSize = sizeof(windowClass);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = "DirectX12初期化";
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

	//DirectX12の初期化-----------------------------------

	UINT digixFactoryFlags = 0;

	//デバッグレイヤーを有効にする
	{
		ComPtr<ID3D12Debug> debugController;
		if( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
		{
			debugController->EnableDebugLayer();

			//デバッグ用のフラグを追加
			digixFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}

	//DirectX12がサポートする利用可能なハードウエアアダプタを取得
	ComPtr<IDXGIFactory4> factory;
	if( FAILED( CreateDXGIFactory2( digixFactoryFlags,IID_PPV_ARGS(&factory) ) ) )
	{
		return -1;
	}
	bool useWarpDevice = true;

	ComPtr<ID3D12Device> device;

	if( useWarpDevice )
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		if( FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))) )
		{
			return -1;
		}
		if( FAILED(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&device))))
		{
			return -1;
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
			IID_PPV_ARGS(&device))))
		{
			return -1;
		}
	}

	//コマンドキュー作成
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ComPtr<ID3D12CommandQueue> commandQueue;
	if( FAILED( device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS(&commandQueue) ) ) )
	{
		return -1;
	}


	//スワップチェインを作成
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
		commandQueue.Get(),
		hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain ) ) )
	{
		return -1;
	}

	//フルスクリーンサポートなし
	if( FAILED( factory->MakeWindowAssociation( hwnd, DXGI_MWA_NO_ALT_ENTER ) ) )
	{
		return -1;
	}

	ComPtr<IDXGISwapChain3> swapChain3;
	if( FAILED( swapChain.As(&swapChain3) ) )
	{
		return -1;
	}
	UINT frameIndex = swapChain3->GetCurrentBackBufferIndex();

	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	UINT rtvDespriptorSize = 0;
	//記述子ヒープ作成
	{
		//レンダーターゲットビュー用の記述子ヒープ作成
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = 2;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if( FAILED( device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)) ) )
		{
			return -1;
		}
		rtvDespriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	ComPtr<ID3D12Resource> renderTarget[2];
	//フレームリソースの作成
	{
		D3D12_CPU_DESCRIPTOR_HANDLE	rtvHandle = {};
		rtvHandle.ptr = rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr;

		//フレームバッファとバックバッファのレンダーターゲットビューを作成
		for(UINT n = 0;n < 2;n++)
		{
			if( FAILED( swapChain3->GetBuffer( n, IID_PPV_ARGS( &renderTarget[n] ) ) ) )
			{
				return -1;
			}
			device->CreateRenderTargetView( renderTarget[n].Get(), nullptr, rtvHandle );
			rtvHandle.ptr += rtvDespriptorSize;

		}
	}
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	if( FAILED( device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)) ) )
	{
		return -1;
	}

	ComPtr<ID3D12GraphicsCommandList> commandList;
	//コマンドリスト作成
	if( FAILED( device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS( &commandList ) ) ) )
	{
		return -1;
	}

	if( FAILED( commandList->Close() ) )
	{
		return -1;
	}

	ComPtr<ID3D12Fence> fence;
	UINT fenceValue;
	HANDLE fenceEvent;
	//同期オブジェクトを作成してリソースがGPUがアップロードされるまで待つ
	{
		if( FAILED( device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence) ) ) )
		{
			return -1;
		}
		fenceValue = 1;

		fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
		if(fenceEvent == nullptr)
		{
			if( FAILED( HRESULT_FROM_WIN32( GetLastError() ) ) )
			{
				return -1;
			}
		}
	}

	ShowWindow(hwnd,SW_SHOW);
	UpdateWindow(hwnd);

	MSG msg;
	ComPtr<ID3D12PipelineState> pipelineState;
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
			if(FAILED( commandAllocator->Reset() ))
			{
				return -1;
			}

			if( FAILED( commandList->Reset( commandAllocator.Get(), pipelineState.Get() ) ) )
			{
				return -1;
			}

			//バックバッファをレンダーターゲットとして指定
			{
				D3D12_RESOURCE_BARRIER resourceBarrier = {};
				resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				resourceBarrier.Transition.pResource = renderTarget[frameIndex].Get();
				resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
				resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				commandList->ResourceBarrier( 1, &resourceBarrier );
			}

			D3D12_CPU_DESCRIPTOR_HANDLE	rtvHandle = {};
			rtvHandle.ptr = rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr+frameIndex*rtvDespriptorSize;
			commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

			//バックバッファ描画
			const float clearColor[] = { 1.0f, 0.0f, 0.0f, 1.0f };
			commandList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );

			//バックバッファを表示
			{
				D3D12_RESOURCE_BARRIER resourceBarrier = {};
				resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				resourceBarrier.Transition.pResource = renderTarget[frameIndex].Get();
				resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
				resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
				resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				commandList->ResourceBarrier( 1, &resourceBarrier );
			}

			if( FAILED( commandList->Close() ) )
			{
				return -1;
			}

			//コマンドリストを実行
			ID3D12CommandList* ppCommandList[] = {commandList.Get()};
			commandQueue->ExecuteCommandLists(_countof(ppCommandList), ppCommandList);

			if( FAILED( swapChain3->Present(1,0) ) )
			{
				return -1;
			}


			//簡単に実装するために続行する前にフレームが終わるまで待つ
			const UINT64 fenceNum = fenceValue;
			if( FAILED( commandQueue->Signal( fence.Get(), fenceNum ) ) )
			{
				return -1;
			}
			fenceValue++;
			if( fence->GetCompletedValue() < fenceNum )
			{
				if( FAILED( fence->SetEventOnCompletion( fenceNum, fenceEvent ) ))
				{
					return -1;
				}
				WaitForSingleObject(fenceEvent,INFINITE);
			}
			frameIndex = swapChain3->GetCurrentBackBufferIndex();
		}
	}

	const UINT64 fenceNum = fenceValue;
	if( FAILED( commandQueue->Signal( fence.Get(), fenceNum ) ) )
	{
		return -1;
	}
	fenceValue++;
	if( fence->GetCompletedValue() < fenceNum )
	{
		if( FAILED( fence->SetEventOnCompletion( fenceNum, fenceEvent ) ))
		{
			return -1;
		}
		WaitForSingleObject(fenceEvent,INFINITE);
	}
	frameIndex = swapChain3->GetCurrentBackBufferIndex();

	return 0;
}

LRESULT CALLBACK WindowProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
	return DefWindowProc(hWnd, message, wParam, lParam);
}