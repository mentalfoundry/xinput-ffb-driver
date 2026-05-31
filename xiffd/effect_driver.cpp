
//	Force Feedback Driver for XInput

#include	"effect_driver.h"

//----------------------------------------------------------------------------------------------
//	CEffectDriver
//----------------------------------------------------------------------------------------------
CEffectDriver::CEffectDriver( VOID )
{
	//	参照カウンタを初期化する
	ReferenceCount	= 1;

	//	デバイス インターフェイス
	DeviceInterface	= NULL;

	//	ワーカ スレッド
	WorkerThread	= NULL;

	//	エフェクト番号
	EffectIndex		= 1;
	//	エフェクト数
	EffectCount		= 0;
	//	エフェクト リスト
	EffectList		= NULL;

	//	停止
	Stopped			= TRUE;
	//	一時停止
	Paused			= FALSE;
	//	一時停止した時刻
	PausedTime		= 0;

	//	ゲイン
	Gain			= 10000;
	//	アクチュエータ
	Actuator		= TRUE;

	//	終了シグナル
	Quit			= FALSE;
}

//----------------------------------------------------------------------------------------------
//	QueryInterface
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::QueryInterface(
	 REFIID		InterfaceID
	,PVOID *	Interface )
{
	//	変数宣言
	HRESULT	Result	= S_OK;

	//	自身のインターフェイスを返す
	if(	IsEqualIID( InterfaceID, IID_IUnknown )
	||	IsEqualIID( InterfaceID, IID_IDirectInputEffectDriver ) )
	{
		AddRef();
		*Interface	= this;
	} else {
		*Interface	= NULL;
		Result		= E_NOINTERFACE;
	}

	return( Result );
}

//----------------------------------------------------------------------------------------------
//	AddRef
//----------------------------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE CEffectDriver::AddRef( VOID )
{
	//	参照カウンタを加算する
	return( InterlockedIncrement( &ReferenceCount ) );
}

//----------------------------------------------------------------------------------------------
//	Release
//----------------------------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE CEffectDriver::Release( VOID )
{
	//	参照カウンタを減算する
	if( InterlockedDecrement( &ReferenceCount ) == 0 )
	{
		//	ドライバの状態を終了状態にする
		Quit	= TRUE;

		//	ワーカー スレッドが終了するのを待つ
		for(;;)
		{
			DWORD	ExitCode;
			GetExitCodeThread( WorkerThread, &ExitCode );
			if( ExitCode != STILL_ACTIVE )
			{
				//	スレッドを閉じる
				CloseHandle( WorkerThread );
				break;
			}
			//	1ms 待つ
			Sleep( 1 );
		}

		delete this;
		return( 0 );
	}

	return( ReferenceCount );
}

//----------------------------------------------------------------------------------------------
//	DeviceID
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::DeviceID(
	 DWORD	DiVersion
	,DWORD	ExternalID
	,DWORD	Begin
	,DWORD	InternalID
	,LPVOID	DiHIDFFInitInfo )
{
	//	デバイス インターフェイスを退避する
	WCHAR *	SrcDeviceInterface	= ((DIHIDFFINITINFO *)DiHIDFFInitInfo)->pwszDeviceInterface;
	LONG	DeviceInterfaceSize	= wcslen( SrcDeviceInterface ) + 1;
	DeviceInterface	= new WCHAR[DeviceInterfaceSize];
	wcscpy_s( DeviceInterface, DeviceInterfaceSize, SrcDeviceInterface );

	//	ワーカー スレッドを開始する
	DWORD	ThreadID;
	WorkerThread	= CreateThread(
						 NULL
						,0
						,EffectProc
						,(LPVOID)this
						,0
						,&ThreadID );
	if( WorkerThread == NULL )
	{
		return( E_FAIL );
	}

	return( S_OK );
}

//----------------------------------------------------------------------------------------------
//	GetVersions
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::GetVersions( LPDIDRIVERVERSIONS DriverVersions )
{
	//	ドライバ バージョンのサイズをチェックする
	if( DriverVersions->dwSize != sizeof( DIDRIVERVERSIONS ) )
	{
		return( E_INVALIDARG );
	}

	//	ドライバ バージョンを返す
	DriverVersions->dwFirmwareRevision	= 1;
	DriverVersions->dwHardwareRevision	= 1;
	DriverVersions->dwFFDriverVersion	= 1;

	return( S_OK );
}

//----------------------------------------------------------------------------------------------
//	Escape
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::Escape(
	 DWORD			ExternalID
	,DWORD			Effect
	,LPDIEFFESCAPE	DIEffEscape )
{
	//	未サポート
	return( E_NOTIMPL );
}

//----------------------------------------------------------------------------------------------
//	SetGain
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::SetGain(
	 DWORD	ExternalID
	,DWORD	NewGain )
{
	//	変数宣言
	HRESULT	Result	= S_OK;

	//	クリティカル セクションに入る
	EnterCriticalSection( &CriticalSection );

	//	ゲインを設定する
	if( 1 <= NewGain && NewGain <= 10000 )
	{
		Gain	= NewGain;
	} else {
		Gain	= max( 1, min( NewGain, 10000 ) );
		Result	= DI_TRUNCATED;
	}

	//	クリティカル セクションを出る
	LeaveCriticalSection( &CriticalSection );

	return( Result );
}

//----------------------------------------------------------------------------------------------
//	SendForceFeedbackCommand
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::SendForceFeedbackCommand(
	 DWORD	ExternalID
	,DWORD	Command )
{
	//	変数宣言
	HRESULT	Result	= S_OK;

	//	クリティカル セクションに入る
	EnterCriticalSection( &CriticalSection );

	//	コマンドによって処理を振り分ける
	switch( Command ) {

		case DISFFC_RESET:
			//	全てのエフェクトを削除する
			for( LONG Index = 0; Index < EffectCount; Index ++ )
			{
				delete EffectList[Index];
			}
			//	エフェクト数を 0 にする
			EffectCount	= 0;
			//	エフェクト リストを削除する
			free( EffectList );
			EffectList	= NULL;
			//	再生を停止する
			Stopped	= TRUE;
			//	一時停止を解除する
			Paused	= FALSE;
			break;

		case DISFFC_STOPALL:
			//	全てのエフェクトの再生を停止する
			for( LONG Index = 0; Index < EffectCount; Index ++ )
			{
				EffectList[Index]->Status	= NULL;
			}
			//	再生を停止する
			Stopped	= TRUE;
			//	一時停止を解除する
			Paused	= FALSE;
			break;

		case DISFFC_PAUSE:
			//	再生を一時停止する
			Paused		= TRUE;
			PausedTime	= GetTickCount();
			break;

		case DISFFC_CONTINUE:
			//	一時停止を解除する
			for( LONG Index = 0; Index < EffectCount; Index ++ )
			{
				//	一時停止した時間だけエフェクトの開始時間を遅らせる
				EffectList[Index]->StartTime	+= ( GetTickCount() - PausedTime );
			}
			Paused	= FALSE;
			break;

		case DISFFC_SETACTUATORSON:
			//	アクチュエータを有効にする
			Actuator	= TRUE;
			break;

		case DISFFC_SETACTUATORSOFF:
			//	アクチュエータを無効にする
			Actuator	= FALSE;
			break;

		default:
			Result	= E_NOTIMPL;
			break;
	}

	//	クリティカル セクションを出る
	LeaveCriticalSection( &CriticalSection );

	return( Result );
}

//----------------------------------------------------------------------------------------------
//	GetForceFeedbackState
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::GetForceFeedbackState(
	 DWORD				ExternalID
	,LPDIDEVICESTATE	DeviceState )
{
	//	デバイス状態のサイズをチェックする
	if( DeviceState->dwSize != sizeof( DIDEVICESTATE ) )
	{
		return( E_INVALIDARG );
	}

	//	クリティカル セクションに入る
	EnterCriticalSection( &CriticalSection );

	//	エフェクト ドライバの状態を返す
	DeviceState->dwState	= NULL;
	//	エフェクト リストが空かどうか
	if( EffectCount == 0 )
	{
		DeviceState->dwState	|= DIGFFS_EMPTY;
	}
	//	エフェクトが停止中かどうか
	if( Stopped == TRUE )
	{
		DeviceState->dwState	|= DIGFFS_STOPPED;
	}
	//	エフェクトが一時停止中かどうか
	if( Paused == TRUE )
	{
		DeviceState->dwState	|= DIGFFS_PAUSED;
	}
	//	アクチュエータが作動しているかどうか
	if( Actuator == TRUE )
	{
		DeviceState->dwState	|= DIGFFS_ACTUATORSON;
	} else {
		DeviceState->dwState	|= DIGFFS_ACTUATORSOFF;
	}
	//	電源はオン固定
	DeviceState->dwState	|= DIGFFS_POWERON;
	//	セーフティ スイッチはオフ固定
	DeviceState->dwState	|= DIGFFS_SAFETYSWITCHOFF;
	//	ユーザー スイッチはオン固定
	DeviceState->dwState	|= DIGFFS_USERFFSWITCHON;

	//	ロード可能数は無制限固定
	DeviceState->dwLoad		= 0;

	//	クリティカル セクションを出る
	LeaveCriticalSection( &CriticalSection );

	return( S_OK );
}

//----------------------------------------------------------------------------------------------
//	DownloadEffect
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::DownloadEffect(
	 DWORD			ExternalID
	,DWORD			EffectType
	,LPDWORD		EffectHandle
	,LPCDIEFFECT	DiEffect
	,DWORD			Flags )
{
	//	変数宣言
	CEffect *	Effect;

	//	DIEP_NODOWNLOAD の場合は無視する
	if( Flags & DIEP_NODOWNLOAD )
	{
		return( S_OK );
	}

	//	クリティカル セクションに入る
	EnterCriticalSection( &CriticalSection );

	if( *EffectHandle == NULL )
	{
		//	新規エフェクトを作成する
		Effect	= new CEffect();
		Effect->Handle	= ( EffectIndex ++ );
		//	エフェクト数を増やす
		EffectCount	++;
		//	エフェクト リストを拡張する
		CEffect * *	NewEffectList;
		NewEffectList	= (CEffect * *)realloc( EffectList, sizeof( CEffect * ) * EffectCount );
		if( NewEffectList != NULL )
		{
			EffectList	= NewEffectList;
		} else {
			//	クリティカル セクションを出る
			LeaveCriticalSection( &CriticalSection );
			//	エラーを返す
			return( E_OUTOFMEMORY );
		}
		EffectList[EffectCount - 1]	= Effect;
		//	エフェクトのハンドルを返す
		*EffectHandle	= Effect->Handle;
	} else {
		//	ハンドルからエフェクトを検索する
		for( LONG Index = 0; Index < EffectCount; Index ++ )
		{
			if( EffectList[Index]->Handle == *EffectHandle )
			{
				Effect	= EffectList[Index];
				break;
			}
		}
	}

	//	種類を設定する
	Effect->Type	= EffectType;

	//	Flags を設定する
	Effect->DiEffect.dwFlags	= DiEffect->dwFlags;

	//	DIEP_DURATION を設定する
	if( Flags & DIEP_DURATION )
	{
		Effect->DiEffect.dwDuration	= DiEffect->dwDuration;
	}

	//	DIEP_SAMPLEPERIOD を設定する
	if( Flags & DIEP_SAMPLEPERIOD )
	{
		Effect->DiEffect.dwSamplePeriod	= DiEffect->dwSamplePeriod;
	}

	//	DIEP_GAIN を設定する
	if( Flags & DIEP_GAIN )
	{
		Effect->DiEffect.dwGain	= DiEffect->dwGain;
	}

	//	DIEP_TRIGGERBUTTON を設定する
	if( Flags & DIEP_TRIGGERBUTTON )
	{
		Effect->DiEffect.dwTriggerButton	= DiEffect->dwTriggerButton;
	}

	//	DIEP_TRIGGERREPEATINTERVAL を設定する
	if( Flags & DIEP_TRIGGERREPEATINTERVAL )
	{
		Effect->DiEffect.dwTriggerRepeatInterval	= DiEffect->dwTriggerRepeatInterval;
	}

	//	DIEP_AXES を設定する
	if( Flags & DIEP_AXES )
	{
		Effect->DiEffect.cAxes		= DiEffect->cAxes;
		Effect->DiEffect.rgdwAxes	= NULL;
	}

	//	DIEP_DIRECTION を設定する
	if( Flags & DIEP_DIRECTION )
	{
		Effect->DiEffect.cAxes			= DiEffect->cAxes;
		Effect->DiEffect.rglDirection	= NULL;
	}

	//	DIEP_ENVELOPE を設定する
	if( ( Flags & DIEP_ENVELOPE ) && DiEffect->lpEnvelope != NULL )
	{
		CopyMemory( &Effect->DiEnvelope, DiEffect->lpEnvelope, sizeof( DIENVELOPE ) );
		//	アタック時間とフェード時間がかぶる場合は値を補正する
		if( Effect->DiEffect.dwDuration - Effect->DiEnvelope.dwFadeTime
				< Effect->DiEnvelope.dwAttackTime )
		{
			Effect->DiEnvelope.dwFadeTime	= Effect->DiEnvelope.dwAttackTime;
		}
		Effect->DiEffect.lpEnvelope	= &Effect->DiEnvelope;
	}

	//	TypeSpecificParams を設定する
	Effect->DiEffect.cbTypeSpecificParams	= DiEffect->cbTypeSpecificParams;

	//	DIEP_TYPESPECIFICPARAMS を設定する
	if( Flags & DIEP_TYPESPECIFICPARAMS )
	{
		switch( EffectType )
		{
			case SPRING:
			case DAMPER:
			case INERTIA:
			case FRICTION:
				Effect->DiEffect.lpvTypeSpecificParams	= NULL;
				break;

			case CONSTANT_FORCE:
				CopyMemory(
					 &Effect->DiConstantForce
					,DiEffect->lpvTypeSpecificParams
					,DiEffect->cbTypeSpecificParams );
				Effect->DiEffect.lpvTypeSpecificParams	= &Effect->DiConstantForce;
				break;

			case CUSTOM_FORCE:
				Effect->DiEffect.lpvTypeSpecificParams	= NULL;
				break;

			case SQUARE:
			case SINE:
			case TRIANGLE:
			case SAWTOOTH_UP:
			case SAWTOOTH_DOWN:
				CopyMemory(
					 &Effect->DiPeriodic
					,DiEffect->lpvTypeSpecificParams
					,DiEffect->cbTypeSpecificParams );
				Effect->DiEffect.lpvTypeSpecificParams	= &Effect->DiPeriodic;
				break;

			case RAMP_FORCE:
				CopyMemory(
					 &Effect->DiRampforce
					,DiEffect->lpvTypeSpecificParams
					,DiEffect->cbTypeSpecificParams );
				Effect->DiEffect.lpvTypeSpecificParams	= &Effect->DiRampforce;
				break;
		}
	}

	//	DIEP_STARTDELAY を設定する
	if( Flags & DIEP_STARTDELAY )
	{
		Effect->DiEffect.dwStartDelay	= DiEffect->dwStartDelay;
	}

	//	DIEP_START が指定されていればエフェクトを再生する
	if( Flags & DIEP_START )
	{
		Effect->Status		= DIEGES_PLAYING;
		Effect->PlayCount	= 1;
		Effect->StartTime	= GetTickCount();
	}

	//	DIEP_NORESTART は無視する
	if( Flags & DIEP_NORESTART )
	{
		;
	}

	//	クリティカル セクションを出る
	LeaveCriticalSection( &CriticalSection );

	return( S_OK );
}

//----------------------------------------------------------------------------------------------
//	DestroyEffect
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::DestroyEffect(
	 DWORD	ExternalID
	,DWORD	EffectHandle )
{
	//	クリティカル セクションに入る
	EnterCriticalSection( &CriticalSection );

	//	ハンドルからエフェクトを検索する
	for( LONG Index = 0; Index < EffectCount; Index ++ )
	{
		if( EffectList[Index]->Handle == EffectHandle )
		{
			//	エフェクトを削除する
			delete EffectList[Index];
			//	エフェクト数を減らす
			EffectCount	--;
			if( EffectCount > 0 )
			{
				//	エフェクト リストを縮小する
				CopyMemory(
					 &EffectList[Index]
					,&EffectList[Index + 1]
					,sizeof( CEffect * ) * ( EffectCount - Index ) );
				CEffect * *	NewEffectList;
				NewEffectList	= (CEffect * *)realloc( EffectList, sizeof( CEffect * ) * EffectCount );
				if( NewEffectList != NULL )
				{
					EffectList	= NewEffectList;
				} else {
					//	クリティカル セクションを出る
					LeaveCriticalSection( &CriticalSection );
					//	エラーを返す
					return( E_OUTOFMEMORY );
				}
			} else {
				//	エフェクト リストを削除する
				free( EffectList );
				EffectList	= NULL;
			}
			break;
		}
	}

	//	クリティカル セクションを出る
	LeaveCriticalSection( &CriticalSection );

	return( S_OK );
}

//----------------------------------------------------------------------------------------------
//	StartEffect
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::StartEffect(
	 DWORD	ExternalID
	,DWORD	EffectHandle
	,DWORD	Mode
	,DWORD	Count )
{
	//	クリティカル セクションに入る
	EnterCriticalSection( &CriticalSection );

	//	ハンドルからエフェクトを検索する
	for( LONG Index = 0; Index < EffectCount; Index ++ )
	{
		if( EffectList[Index]->Handle == EffectHandle )
		{
			//	エフェクトの再生を開始する
			EffectList[Index]->Status		= DIEGES_PLAYING;
			EffectList[Index]->PlayCount	= Count;
			EffectList[Index]->StartTime	= GetTickCount();
			//	停止を解除する
			Stopped	= FALSE;
		} else {
			//	DIES_SOLO が指定されている場合は他のエフェクトを停止する
			if( Mode & DIES_SOLO )
			{
				EffectList[Index]->Status	= NULL;
			}
		}
	}

	//	クリティカル セクションを出る
	LeaveCriticalSection( &CriticalSection );

	return( S_OK );
}

//----------------------------------------------------------------------------------------------
//	StopEffect
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::StopEffect(
	 DWORD	ExternalID
	,DWORD	EffectHandle )
{
	//	クリティカル セクションに入る
	EnterCriticalSection( &CriticalSection );

	//	ハンドルからエフェクトを検索する
	for( LONG Index = 0; Index < EffectCount; Index ++ )
	{
		if( EffectList[Index]->Handle == EffectHandle )
		{
			//	エフェクトを停止する
			EffectList[Index]->Status	= NULL;
			break;
		}
	}

	//	クリティカル セクションを出る
	LeaveCriticalSection( &CriticalSection );

	return( S_OK );
}

//----------------------------------------------------------------------------------------------
//	GetEffectStatus
//----------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CEffectDriver::GetEffectStatus(
	 DWORD		ExternalID
	,DWORD		EffectHandle
	,LPDWORD	Status )
{
	//	クリティカル セクションに入る
	EnterCriticalSection( &CriticalSection );

	//	ハンドルからエフェクトを検索する
	for( LONG Index = 0; Index < EffectCount; Index ++ )
	{
		if( EffectList[Index]->Handle == EffectHandle )
		{
			//	エフェクトの状態を返す
			*Status	= EffectList[Index]->Status;
			break;
		}
	}

	//	クリティカル セクションを出る
	LeaveCriticalSection( &CriticalSection );

	return( S_OK );
}

//----------------------------------------------------------------------------------------------
//	GetControllerID
//----------------------------------------------------------------------------------------------
LONG CEffectDriver::GetControllerID()
{
	//	変数宣言
	HRESULT	Result;
	LONG	ControllerID	= -1;
	LONG	FoundIndex		= 0;

	//	デバイス インターフェイスを整形する
	_wcsupr( DeviceInterface );
	DeviceInterface	= wcsstr( DeviceInterface, L"HID" );
	wcschr( DeviceInterface, L'#' )[0]	= L'\\';
	wcschr( DeviceInterface, L'#' )[0]	= L'\\';
	wcschr( DeviceInterface, L'#' )[0]	= NULL;

	//	COM を初期化する
	Result	= CoInitialize( NULL );
	if( FAILED( Result ) )
	{
		return( -1 );
	}

	//	WMI を作成する
	IWbemLocator *	Locator		= NULL;
	Result	= CoCreateInstance(
				 _uuidof( WbemLocator )
				,NULL
				,CLSCTX_INPROC_SERVER
				,_uuidof( IWbemLocator )
				,(LPVOID *)&Locator );
	if( FAILED( Result ) || Locator == NULL )
	{
		goto Cleanup;
	}

	//	WMI に接続する
	IWbemServices *	Services	= NULL;
	BSTR	NameSpace	= SysAllocString( L"\\\\.\\root\\cimv2" );
	BSTR	ClassName	= SysAllocString( L"Win32_PNPEntity" );
	BSTR	DeviceID	= SysAllocString( L"DeviceID" );
	Result	= Locator->ConnectServer(
				 NameSpace
				,NULL
				,NULL
				,0L
				,0L
				,NULL
				,NULL
				,&Services );
	if( FAILED( Result ) || Services == NULL )
	{
		goto Cleanup;
	}

	//	セキュリティ レベルを変更する
	CoSetProxyBlanket(
		 Services
		,RPC_C_AUTHN_WINNT
		,RPC_C_AUTHZ_NONE
		,NULL
		,RPC_C_AUTHN_LEVEL_CALL
		,RPC_C_IMP_LEVEL_IMPERSONATE
		,NULL
		,EOAC_NONE );

	//	すべてのデバイスを列挙する
	IEnumWbemClassObject *	EnumDevices	= NULL;
	Services->CreateInstanceEnum( ClassName, 0, NULL, &EnumDevices );
	if( FAILED( Result ) || EnumDevices == NULL )
	{
		goto Cleanup;
	}
	for(;;)
	{
		//	デバイスを 20 個ずつ取得する
		IWbemClassObject *	Devices[20]	= { NULL };
		DWORD				Returned	= 0;
		Result	= EnumDevices->Next( 10000, 20, Devices, &Returned );
		if( FAILED( Result ) )
		{
			goto Cleanup;
		}
		//	取得できなければ終了する
		if( Returned == 0 )
		{
			break;
		}

		//	取得したデバイスずつ処理する
		for( DWORD Index = 0; Index < Returned; Index ++ )
		{
			//	デバイスのデバイスＩＤを取得する
			VARIANT	Info;
			Result	= Devices[Index]->Get( DeviceID, 0, &Info, NULL, NULL );
			if( SUCCEEDED( Result ) && Info.vt == VT_BSTR && Info.bstrVal != NULL )
			{
				//	デバイスＩＤに "HID" と "IG_" が入っているかチェックする
				_wcsupr( Info.bstrVal );
				if( wcsstr( Info.bstrVal, L"HID") && wcsstr( Info.bstrVal, L"IG_") )
				{
					//	デバイスＩＤとデバイス インターフェイスが一致するかチェックする
					if( wcscmp( Info.bstrVal, DeviceInterface ) == 0 )
					{
						ControllerID	= FoundIndex;
					}
					FoundIndex	++;
				}
			}
			Devices[Index]->Release();
		}

		//	コントローラが見つかれば終了する
		if( ControllerID != -1 )
		{
			break;
		}
	}

Cleanup:

	//	デバイスの列挙を開放する
	EnumDevices->Release();
	//	WMI への接続を開放する
	Services->Release();
	if( NameSpace != NULL )
	{
		SysFreeString( NameSpace );
	}
	if( ClassName != NULL )
	{
		SysFreeString( ClassName );
	}
	if( DeviceID != NULL )
	{
		SysFreeString( DeviceID );
	}
	//	WMI を開放する
	Locator->Release();

	//	COM を開放する
	CoUninitialize();

	return( ControllerID );
}

//----------------------------------------------------------------------------------------------
//	EffectProc
//----------------------------------------------------------------------------------------------
STDAPI_(DWORD) WINAPI EffectProc( LPVOID EffectDriverInterface )
{
	//	エフェクト ドライバを取得する
	CEffectDriver *	EffectDriver	= (CEffectDriver *)EffectDriverInterface;

	//	コントローラＩＤを取得する
	LONG	ControllerID	= EffectDriver->GetControllerID();
	if( ControllerID == -1 )
	{
		return( 0 );
	}

	//	ライトを点灯させる
	XINPUT_STATE	State;
	XInputGetState( ControllerID, &State );

	//	前回のエフェクトの強さ
	LONG	PrvLeftLevel	= -1;
	LONG	PrvRightLevel	= -1;

	while( EffectDriver->Quit == FALSE )
	{
		//	クリティカル セクションに入る
		EnterCriticalSection( &CriticalSection );

		//	エフェクトの強さ
		LONG	LeftLevel	= 0;
		LONG	RightLevel	= 0;
		LONG	Gain		= EffectDriver->Gain;

		if( EffectDriver->Actuator == TRUE )
		{
			//	エフェクトの強さを計算する
			for( LONG Index = 0; Index < EffectDriver->EffectCount; Index ++ )
			{
				EffectDriver->EffectList[Index]->Calc( &LeftLevel, &RightLevel );
			}
		}

		//	クリティカル セクションを出る
		LeaveCriticalSection( &CriticalSection );

		//	コントローラーの振動を設定する
		if( PrvLeftLevel != LeftLevel || PrvRightLevel != RightLevel )
		{
			XINPUT_VIBRATION	Vibration;
			Vibration.wLeftMotorSpeed	= (WORD)min( 65535, LeftLevel * Gain / 10000 );
			Vibration.wRightMotorSpeed	= (WORD)min( 65535, RightLevel * Gain / 10000 );
			XInputSetState( ControllerID, &Vibration );
		}

		//	エフェクトの強さを退避する
		PrvLeftLevel	= LeftLevel;
		PrvRightLevel	= RightLevel;

		//	2ms 待機する
		Sleep( 2 );
	}

	return( 0 );
}
