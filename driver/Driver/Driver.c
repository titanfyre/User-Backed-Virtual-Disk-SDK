#include <ntifs.h>
#include <storport.h>
#include <ntddscsi.h>
#include "../SDK/DiskProtocol.h"
#include "../Common/DriverAuthorization.h"
#define TAG 'dBuT'
#define TICKS 10000000ull
typedef struct SLOT {PSCSI_REQUEST_BLOCK Srb;ULONGLONG Id,Deadline,Offset;ULONG Bytes,Operation;BOOLEAN Issued;} SLOT;
typedef struct ADAPTER {
 KSPIN_LOCK Lock,ServiceLock;KMUTEX Control;IO_CSQ Csq;LIST_ENTRY Waiters;BOOLEAN Initialized,PollReserved,OwnsSlot;volatile LONG Running,Active,Removed,OwnerLost,DepthConfigured,DepthAttempts;
 PFILE_OBJECT Owner;PEPROCESS Process;TITAN_AUTHORIZATION_STATE Authorization;
 ULONGLONG Capacity,Generation,NextId,PollEpoch;BOOLEAN Readonly,DpcInitialized;ULONG ConfigBits;SLOT Slots[UD_PENDING];UD_STATISTICS Stats;STOR_DPC StopDpc;volatile LONG StopQueued;
} ADAPTER;
static volatile LONG AdapterClaim;
DRIVER_INITIALIZE DriverEntry;
VIRTUAL_HW_FIND_ADAPTER FindAdapter;
HW_INITIALIZE Initialize;
HW_STARTIO StartIo;
HW_RESET_BUS ResetBus;
HW_ADAPTER_CONTROL AdapterControl;
HW_FREE_ADAPTER_RESOURCES FreeAdapter;
HW_PROCESS_SERVICE_REQUEST ServiceRequest;
HW_COMPLETE_SERVICE_IRP CompleteServices;
HW_TIMER Timer;
HW_PASSIVE_INITIALIZE_ROUTINE PassiveInitialize;
IO_CSQ_INSERT_IRP CsqInsert;
IO_CSQ_REMOVE_IRP CsqRemove;
IO_CSQ_PEEK_NEXT_IRP CsqPeek;
_Function_class_(IO_CSQ_ACQUIRE_LOCK) _IRQL_requires_max_(DISPATCH_LEVEL) _IRQL_raises_(DISPATCH_LEVEL)
VOID CsqAcquire(_In_ PIO_CSQ q,_Out_ PKIRQL old);
_Function_class_(IO_CSQ_RELEASE_LOCK) _IRQL_requires_(DISPATCH_LEVEL)
VOID CsqRelease(_In_ PIO_CSQ q,_In_ _IRQL_restores_ KIRQL old);
IO_CSQ_COMPLETE_CANCELED_IRP CsqCancel;
HW_DPC_ROUTINE StopDpc;
static ADAPTER* FromCsq(PIO_CSQ q){return CONTAINING_RECORD(q,ADAPTER,Csq);}
static PFILE_OBJECT RequestFile(PIRP irp){PFILE_OBJECT original=irp->Tail.Overlay.OriginalFileObject;return original?original:IoGetCurrentIrpStackLocation(irp)->FileObject;}
static VOID FinishService(ADAPTER* a,PIRP irp,NTSTATUS status,ULONG bytes){irp->IoStatus.Status=status;irp->IoStatus.Information=NT_SUCCESS(status)?bytes:0;(VOID)StorPortCompleteServiceIrp(a,irp);}
_Use_decl_annotations_ VOID CsqInsert(PIO_CSQ q,PIRP irp){InsertTailList(&FromCsq(q)->Waiters,&irp->Tail.Overlay.ListEntry);}
_Use_decl_annotations_ VOID CsqRemove(PIO_CSQ q,PIRP irp){UNREFERENCED_PARAMETER(q);RemoveEntryList(&irp->Tail.Overlay.ListEntry);}
_Use_decl_annotations_ PIRP CsqPeek(PIO_CSQ q,PIRP irp,PVOID context){ADAPTER* a=FromCsq(q);PLIST_ENTRY head=&a->Waiters,next=irp?irp->Tail.Overlay.ListEntry.Flink:head->Flink;while(next!=head){PIRP item=CONTAINING_RECORD(next,IRP,Tail.Overlay.ListEntry);ULONG_PTR epoch=(ULONG_PTR)item->Tail.Overlay.DriverContext[0];if(!context||(context==(PVOID)MAXULONG_PTR?epoch!=(ULONG_PTR)a->Generation:epoch==(ULONG_PTR)context))return item;next=next->Flink;}return NULL;}
_Use_decl_annotations_ VOID CsqAcquire(PIO_CSQ q,PKIRQL old){KeAcquireSpinLock(&FromCsq(q)->ServiceLock,old);}
_Use_decl_annotations_ VOID CsqRelease(PIO_CSQ q,KIRQL old){KeReleaseSpinLock(&FromCsq(q)->ServiceLock,old);}
_Use_decl_annotations_ VOID CsqCancel(PIO_CSQ q,PIRP irp){ADAPTER* a=FromCsq(q);KIRQL old;KeAcquireSpinLock(&a->Lock,&old);if(a->PollEpoch==(ULONGLONG)(ULONG_PTR)irp->Tail.Overlay.DriverContext[0])a->PollReserved=FALSE;KeReleaseSpinLock(&a->Lock,old);FinishService(a,irp,STATUS_CANCELLED,0);}
static ULONG Be16(const UCHAR* p){return ((ULONG)p[0]<<8)|p[1];}
static ULONG Be32(const UCHAR* p){return ((ULONG)p[0]<<24)|((ULONG)p[1]<<16)|((ULONG)p[2]<<8)|p[3];}
static ULONGLONG Be64(const UCHAR* p){return ((ULONGLONG)Be32(p)<<32)|Be32(p+4);}
static VOID Put32(UCHAR* p,ULONG n){p[0]=(UCHAR)(n>>24);p[1]=(UCHAR)(n>>16);p[2]=(UCHAR)(n>>8);p[3]=(UCHAR)n;}
static VOID Put64(UCHAR* p,ULONGLONG n){Put32(p,(ULONG)(n>>32));Put32(p+4,(ULONG)n);}
static VOID Sense(PSCSI_REQUEST_BLOCK s,UCHAR key,UCHAR asc){UCHAR bytes[18]={0};ULONG n=min((ULONG)s->SenseInfoBufferLength,(ULONG)sizeof(bytes));bytes[0]=0x70;bytes[2]=key;bytes[7]=10;bytes[12]=asc;s->SrbStatus=SRB_STATUS_ERROR;s->ScsiStatus=SCSISTAT_CHECK_CONDITION;s->DataTransferLength=0;if(s->SenseInfoBuffer&&n){RtlCopyMemory(s->SenseInfoBuffer,bytes,n);s->SrbStatus|=SRB_STATUS_AUTOSENSE_VALID;}}
static VOID Reply(PSCSI_REQUEST_BLOCK s,const UCHAR* data,ULONG bytes,ULONG allocation){ULONG n=min(bytes,min(s->DataTransferLength,allocation));if(n&&!s->DataBuffer){s->SrbStatus=SRB_STATUS_INVALID_REQUEST;s->DataTransferLength=0;return;}if(n)RtlCopyMemory(s->DataBuffer,data,n);s->DataTransferLength=n;s->SrbStatus=SRB_STATUS_SUCCESS;s->ScsiStatus=SCSISTAT_GOOD;}
static VOID CompleteSrb(ADAPTER* a,PSCSI_REQUEST_BLOCK s){StorPortNotification(RequestComplete,a,s);}
/* Explicit nonblocking process-object query: the object remains referenced by
   the caller and no EPROCESS fields or offsets are inspected. */
_IRQL_requires_max_(DISPATCH_LEVEL)
static BOOLEAN ProcessExited(PEPROCESS process){
 LARGE_INTEGER zero;zero.QuadPart=0;_Analysis_assume_(zero.QuadPart==0);
 /* WDK PREfast models this as APC-only even though wdm.h and the documented
    contract permit explicit Timeout->QuadPart==0 at DISPATCH_LEVEL. */
#pragma warning(suppress:28118)
 return KeWaitForSingleObject(process,Executive,KernelMode,FALSE,&zero)==STATUS_SUCCESS;
}
static BOOLEAN Live(ADAPTER* a){return InterlockedCompareExchange(&a->Running,0,0)&&InterlockedCompareExchange(&a->Active,0,0)&&!InterlockedCompareExchange(&a->OwnerLost,0,0)&&TitanAuthorizationAllowed(&a->Authorization);}
static SLOT* First(ADAPTER* a){SLOT* first=NULL;ULONG i;for(i=0;i<UD_PENDING;++i)if(a->Slots[i].Srb&&(!first||a->Slots[i].Id<first->Id))first=&a->Slots[i];return first;}
/* Queue operations and SRB-buffer access share Lock. Nothing dereferences an
   SRB after it has been removed from its slot and the lock is released. */
static ULONGLONG DrainSrbs(ADAPTER* a,UCHAR status,BOOLEAN portHeld){PSCSI_REQUEST_BLOCK list[UD_PENDING];ULONG n=0,i;KIRQL old;ULONGLONG epoch;STOR_LOCK_HANDLE port={0};if(!portHeld)StorPortAcquireSpinLock(a,StartIoLock,NULL,&port);KeAcquireSpinLock(&a->Lock,&old);epoch=a->Generation;a->Active=FALSE;a->OwnerLost=TRUE;for(i=0;i<UD_PENDING;++i)if(a->Slots[i].Srb){list[n++]=a->Slots[i].Srb;a->Slots[i].Srb=NULL;}a->Stats.pending=0;KeReleaseSpinLock(&a->Lock,old);for(i=0;i<n;++i){list[i]->SrbStatus=status;list[i]->DataTransferLength=0;CompleteSrb(a,list[i]);}if(!portHeld)StorPortReleaseSpinLock(a,&port);return epoch;}
static VOID RetireOwner(ADAPTER* a,ULONGLONG epoch){PFILE_OBJECT file=NULL;PEPROCESS process=NULL;KIRQL old;KeAcquireSpinLock(&a->Lock,&old);if(a->OwnerLost&&a->Generation==epoch){file=a->Owner;process=a->Process;a->Owner=NULL;a->Process=NULL;}KeReleaseSpinLock(&a->Lock,old);/* Closing a FILE_OBJECT may run PASSIVE cleanup; defer that deletion. */if(file)ObDereferenceObjectDeferDelete(file);if(process)ObDereferenceObjectDeferDelete(process);}
static VOID Drain(ADAPTER* a,UCHAR status){PIRP irp;KIRQL old;ULONGLONG epoch=DrainSrbs(a,status,FALSE);while((irp=IoCsqRemoveNextIrp(&a->Csq,(PVOID)(ULONG_PTR)epoch))!=NULL){KeAcquireSpinLock(&a->Lock,&old);if(a->PollEpoch==epoch)a->PollReserved=FALSE;KeReleaseSpinLock(&a->Lock,old);FinishService(a,irp,STATUS_DEVICE_NOT_READY,0);}RetireOwner(a,epoch);}
_Use_decl_annotations_ VOID StopDpc(PSTOR_DPC dpc,PVOID context,PVOID a1,PVOID a2){ADAPTER* a=context;UNREFERENCED_PARAMETER(dpc);UNREFERENCED_PARAMETER(a1);UNREFERENCED_PARAMETER(a2);if(!a)return;Drain(a,SRB_STATUS_NO_DEVICE);InterlockedExchange(&a->StopQueued,0);}
static VOID QueueStop(ADAPTER* a){if(a->DpcInitialized&&InterlockedCompareExchange(&a->StopQueued,1,0)==0){InterlockedExchange(&a->Active,FALSE);InterlockedExchange(&a->OwnerLost,TRUE);if(!StorPortIssueDpc(a,&a->StopDpc,NULL,NULL))InterlockedExchange(&a->StopQueued,0);}}
static VOID Pump(ADAPTER* a){PIRP irp;SLOT* slot=NULL;KIRQL old;BOOLEAN work=FALSE;UD_MESSAGE* m;
 while((irp=IoCsqRemoveNextIrp(&a->Csq,(PVOID)MAXULONG_PTR))!=NULL){KeAcquireSpinLock(&a->Lock,&old);if(a->PollEpoch==(ULONGLONG)(ULONG_PTR)irp->Tail.Overlay.DriverContext[0])a->PollReserved=FALSE;KeReleaseSpinLock(&a->Lock,old);FinishService(a,irp,STATUS_ACCESS_DENIED,0);}
 KeAcquireSpinLock(&a->Lock,&old);slot=First(a);work=(slot&&!slot->Issued)||!Live(a);KeReleaseSpinLock(&a->Lock,old);if(!work)return;
 irp=IoCsqRemoveNextIrp(&a->Csq,NULL);if(!irp)return;m=irp->AssociatedIrp.SystemBuffer;
 KeAcquireSpinLock(&a->Lock,&old);if(a->PollEpoch==(ULONGLONG)(ULONG_PTR)irp->Tail.Overlay.DriverContext[0])a->PollReserved=FALSE;
 if(a->Owner!=RequestFile(irp)||a->Process!=IoGetRequestorProcess(irp)||(ULONGLONG)(ULONG_PTR)irp->Tail.Overlay.DriverContext[0]!=a->Generation){KeReleaseSpinLock(&a->Lock,old);FinishService(a,irp,STATUS_ACCESS_DENIED,0);return;}
 slot=Live(a)?First(a):NULL;if(slot&&slot->Issued)slot=NULL;
 if(!slot){KeReleaseSpinLock(&a->Lock,old);FinishService(a,irp,STATUS_RETRY,0);return;}
 RtlZeroMemory(m,sizeof(*m));m->magic=UD_MAGIC;m->version=UD_VERSION;m->command=UD_POLL;m->generation=a->Generation;m->id=slot->Id;m->offset=slot->Offset;m->length=slot->Bytes;m->operation=slot->Operation;
 if(slot->Operation==UD_WRITE&&slot->Bytes)RtlCopyMemory(m->data,slot->Srb->DataBuffer,slot->Bytes);slot->Issued=TRUE;KeReleaseSpinLock(&a->Lock,old);FinishService(a,irp,STATUS_SUCCESS,sizeof(*m));
}
static BOOLEAN Queue(ADAPTER* a,PSCSI_REQUEST_BLOCK s,ULONG operation,ULONGLONG offset,ULONG bytes){ULONG i;if(a->Stats.pending>=UD_PENDING){s->SrbStatus=SRB_STATUS_ERROR;s->DataTransferLength=0;++a->Stats.rejected;++a->Stats.queue_overflow;return FALSE;}for(i=0;i<UD_PENDING;++i)if(!a->Slots[i].Srb){SLOT* p=&a->Slots[i];p->Srb=s;p->Id=++a->NextId;if(!p->Id)p->Id=++a->NextId;p->Deadline=KeQueryInterruptTime()+5*TICKS;p->Offset=offset;p->Bytes=bytes;p->Operation=operation;p->Issued=FALSE;++a->Stats.pending;if(operation==UD_READ)++a->Stats.reads;else if(operation==UD_WRITE)++a->Stats.writes;else ++a->Stats.flushes;s->SrbStatus=SRB_STATUS_PENDING;return TRUE;}s->SrbStatus=SRB_STATUS_ERROR;s->DataTransferLength=0;return FALSE;}
static BOOLEAN Scsi(ADAPTER* a,PSCSI_REQUEST_BLOCK s){UCHAR op=s->Cdb[0];UCHAR data[96]={0};ULONGLONG block,count;ULONG bytes;
 if(s->PathId||s->TargetId||s->Lun||!a->Active){s->SrbStatus=SRB_STATUS_NO_DEVICE;s->DataTransferLength=0;return FALSE;}
 if(s->CdbLength==0||s->CdbLength>16||((op==0||op==3||op==0x1a||op==0x1b||op==0x1e||op==0x12)&&s->CdbLength!=6)||((op==0x25||op==0x5a||op==0x35||op==0x2f)&&s->CdbLength!=10)||((op==0x9e||op==0x91||op==0x8f)&&s->CdbLength!=16)||(op==0xa0&&s->CdbLength!=12)){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);return FALSE;}
 switch(op){
 case 0x12:if(s->CdbLength!=6){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);break;}if(!(s->Cdb[1]&1)){data[0]=DIRECT_ACCESS_DEVICE;data[1]=0;data[2]=6;data[3]=2;data[4]=31;data[7]=2;RtlCopyMemory(data+8,"TITAN   UserBacked Disk 0100",28);Reply(s,data,36,s->Cdb[4]);}else if(s->Cdb[2]==0){data[1]=0;data[3]=2;data[4]=0;data[5]=0x80;Reply(s,data,6,s->Cdb[4]);}else if(s->Cdb[2]==0x80){data[1]=0x80;data[3]=16;RtlCopyMemory(data+4,"TITAN-USER-DISK01",16);Reply(s,data,20,s->Cdb[4]);}else Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);break;
 case 0:if(Live(a)){s->SrbStatus=SRB_STATUS_SUCCESS;s->DataTransferLength=0;}else Sense(s,SCSI_SENSE_NOT_READY,0x3a);break;
 case 0x03:data[0]=0x70;data[7]=10;Reply(s,data,18,s->Cdb[4]);break;
 case 0x25:Put32(data,(ULONG)(a->Capacity/512-1));Put32(data+4,512);Reply(s,data,8,8);break;
 case 0x9e:if((s->Cdb[1]&31)!=0x10){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);break;}Put64(data,a->Capacity/512-1);Put32(data+8,512);Reply(s,data,32,Be32(s->Cdb+10));break;
 case 0x1a: case 0x5a:{BOOLEAN ten=op==0x5a;ULONG header=ten?8u:4u;if((s->Cdb[1]&~8u)||s->Cdb[3]||(s->Cdb[2]!=0&&s->Cdb[2]!=0x3f)){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);break;}if(ten){data[1]=6;data[3]=a->Readonly?0x80:0;}else{data[0]=3;data[2]=a->Readonly?0x80:0;}Reply(s,data,header,ten?Be16(s->Cdb+7):s->Cdb[4]);break;}
 case 0xa0:data[3]=8;Reply(s,data,16,Be32(s->Cdb+6));break;
 case 0x1b:case 0x1e:s->SrbStatus=SRB_STATUS_SUCCESS;s->DataTransferLength=0;break;
 case 0x35:case 0x91:if(!Live(a)){Sense(s,SCSI_SENSE_NOT_READY,0x3a);break;}return Queue(a,s,UD_FLUSH,0,0);
 case 0x08:case 0x0a:case 0x28:case 0x2a:case 0x88:case 0x8a:
  if(!Live(a)){Sense(s,SCSI_SENSE_NOT_READY,0x3a);break;}
  if(op==0x08||op==0x0a){if(s->CdbLength!=6||(s->Cdb[1]&0xe0)||s->Cdb[5]){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);break;}block=((ULONGLONG)(s->Cdb[1]&31)<<16)|Be16(s->Cdb+2);count=s->Cdb[4]?s->Cdb[4]:256;}
  else if(op==0x28||op==0x2a){if(s->CdbLength!=10||s->Cdb[1]||s->Cdb[6]||s->Cdb[9]){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);break;}block=Be32(s->Cdb+2);count=Be16(s->Cdb+7);}
  else{if(s->CdbLength!=16||s->Cdb[1]||s->Cdb[14]||s->Cdb[15]){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);break;}block=Be64(s->Cdb+2);count=Be32(s->Cdb+10);}
  if(!count){s->SrbStatus=SRB_STATUS_SUCCESS;s->DataTransferLength=0;break;}
  if(count>UD_TRANSFER/512||block>=a->Capacity/512||count>a->Capacity/512-block||!s->DataBuffer){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x21);break;}bytes=(ULONG)count*512;if(bytes>s->DataTransferLength){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);break;}
  if(op==0x0a||op==0x2a||op==0x8a){if((s->SrbFlags&(SRB_FLAGS_DATA_IN|SRB_FLAGS_DATA_OUT))!=SRB_FLAGS_DATA_OUT){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);break;}if(a->Readonly){Sense(s,SCSI_SENSE_DATA_PROTECT,0x27);break;}return Queue(a,s,UD_WRITE,block*512,bytes);}if((s->SrbFlags&(SRB_FLAGS_DATA_IN|SRB_FLAGS_DATA_OUT))!=SRB_FLAGS_DATA_IN){Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x24);break;}return Queue(a,s,UD_READ,block*512,bytes);
 case 0x2f:case 0x8f:Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x20);break;
 default:Sense(s,SCSI_SENSE_ILLEGAL_REQUEST,0x20);break;
 }return FALSE;
}
_Use_decl_annotations_ BOOLEAN StartIo(PVOID extension,PSCSI_REQUEST_BLOCK s){ADAPTER* a=extension;KIRQL old;BOOLEAN pending=FALSE;STOR_LOCK_HANDLE port={0};if(!s)return TRUE;/* Virtual miniport HwStartIo is entered without port locks. */StorPortAcquireSpinLock(a,StartIoLock,NULL,&port);
 /* Storport requires this on the first valid LUN INQUIRY or later, never
    before LUN discovery. Repeat after each new media generation. */
 if(a->Active&&a->Running&&!a->DepthConfigured&&s->Function==SRB_FUNCTION_EXECUTE_SCSI&&s->CdbLength==6&&s->Cdb[0]==0x12&&!s->PathId&&!s->TargetId&&!s->Lun){InterlockedIncrement(&a->DepthAttempts);if(StorPortSetDeviceQueueDepth(a,0,0,0,UD_PENDING))InterlockedExchange(&a->DepthConfigured,1);}
 KeAcquireSpinLock(&a->Lock,&old);s->ScsiStatus=SCSISTAT_GOOD;
 if(!a->Running||a->Removed){s->SrbStatus=SRB_STATUS_NO_DEVICE;s->DataTransferLength=0;}
 else if(s->Function==SRB_FUNCTION_EXECUTE_SCSI)pending=Scsi(a,s);
 else if(s->Function==SRB_FUNCTION_FLUSH||s->Function==SRB_FUNCTION_SHUTDOWN){if(Live(a))pending=Queue(a,s,UD_FLUSH,0,0);else{s->SrbStatus=SRB_STATUS_SUCCESS;s->DataTransferLength=0;}}
 else if(s->Function==SRB_FUNCTION_PNP){PSCSI_PNP_REQUEST_BLOCK p=(PSCSI_PNP_REQUEST_BLOCK)s;if(p->PnPAction==StorQueryCapabilities&&p->DataBuffer&&p->DataTransferLength>=sizeof(STOR_DEVICE_CAPABILITIES)){PSTOR_DEVICE_CAPABILITIES c=p->DataBuffer;RtlZeroMemory(c,sizeof(*c));c->Version=1;c->Removable=TRUE;c->UniqueID=TRUE;c->SurpriseRemovalOK=TRUE;p->DataTransferLength=sizeof(*c);}else p->DataTransferLength=0;s->SrbStatus=SRB_STATUS_SUCCESS;}
 else if(s->Function==SRB_FUNCTION_POWER){s->SrbStatus=SRB_STATUS_SUCCESS;s->DataTransferLength=0;}
 else{s->SrbStatus=SRB_STATUS_INVALID_REQUEST;s->DataTransferLength=0;++a->Stats.rejected;}
 KeReleaseSpinLock(&a->Lock,old);if(!pending)CompleteSrb(a,s);StorPortReleaseSpinLock(a,&port);if(pending)Pump(a);return TRUE;}
_Use_decl_annotations_ VOID Timer(PVOID extension){ADAPTER* a=extension;BOOLEAN lost=FALSE,again;UCHAR reason=SRB_STATUS_NO_DEVICE;ULONG i;KIRQL old;
 KeAcquireSpinLock(&a->Lock,&old);if(a->Owner&&!a->OwnerLost){/* Process is referenced while Lock is held. An explicit zero timeout never
    waits, and is valid at DISPATCH_LEVEL; STATUS_TIMEOUT is also NT_SUCCESS,
    so test only STATUS_SUCCESS. This check runs at 10Hz, never per block I/O. */
  lost=(a->Owner->Flags&FO_CLEANUP_COMPLETE)!=0||!a->Process||ProcessExited(a->Process)||(a->Active&&!TitanAuthorizationAllowed(&a->Authorization));for(i=0;i<UD_PENDING;++i)if(a->Slots[i].Srb&&KeQueryInterruptTime()>=a->Slots[i].Deadline){++a->Stats.timeouts;reason=SRB_STATUS_TIMEOUT;lost=TRUE;break;}}
 again=!a->Removed&&a->Running&&a->Owner&&!a->OwnerLost;KeReleaseSpinLock(&a->Lock,old);
 if(lost){/* HwTimer is entered with StartIoLock already held. */(VOID)DrainSrbs(a,reason,TRUE);QueueStop(a);StorPortNotification(BusChangeDetected,a,0);again=FALSE;}
 if(again)StorPortNotification(RequestTimerCall,a,Timer,100000u);
}
_Use_decl_annotations_ VOID ServiceRequest(PVOID extension,PVOID request){ADAPTER* a=extension;PIRP irp=request;PIO_STACK_LOCATION stack=IoGetCurrentIrpStackLocation(irp);PFILE_OBJECT file=RequestFile(irp);UD_MESSAGE* m=irp->AssociatedIrp.SystemBuffer;UD_HEADER header;NTSTATUS status=STATUS_INVALID_PARAMETER;PFILE_OBJECT oldFile=NULL;PEPROCESS oldProcess=NULL;BOOLEAN notify=FALSE,pend=FALSE;KIRQL old;ULONG i;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL){FinishService(a,irp,STATUS_INVALID_DEVICE_STATE,0);return;}
 if(!m){FinishService(a,irp,STATUS_INVALID_USER_BUFFER,0);return;}
 if(stack->Parameters.DeviceIoControl.InputBufferLength!=sizeof(*m)||stack->Parameters.DeviceIoControl.OutputBufferLength!=sizeof(*m)){FinishService(a,irp,STATUS_INFO_LENGTH_MISMATCH,0);return;}
 if(!file||!IoGetRequestorProcess(irp)){FinishService(a,irp,STATUS_INVALID_HANDLE,0);return;}
 status=IoValidateDeviceIoControlAccess(irp,FILE_READ_DATA|FILE_WRITE_DATA);if(!NT_SUCCESS(status)){FinishService(a,irp,status,0);return;}
 /* Only the fixed header is local; never put a 64KiB message on kernel stack. */
 RtlZeroMemory(&header,64);RtlCopyMemory(&header,m,64);
 if(header.magic!=UD_MAGIC||header.version!=UD_VERSION||header.reserved||header.command>UD_STOP){FinishService(a,irp,STATUS_INVALID_PARAMETER,0);return;}
 KeEnterCriticalRegion();(VOID)KeWaitForSingleObject(&a->Control,Executive,KernelMode,FALSE,NULL);
 KeAcquireSpinLock(&a->Lock,&old);if(a->Removed){KeReleaseSpinLock(&a->Lock,old);status=STATUS_DEVICE_REMOVED;goto end;}
 if(a->StopQueued&&header.command!=UD_STATUS){KeReleaseSpinLock(&a->Lock,old);status=STATUS_DEVICE_NOT_READY;goto end;}
 if(header.command==UD_CHALLENGE&&(!a->Owner||a->OwnerLost)){oldFile=a->Owner;oldProcess=a->Process;a->Owner=file;ObReferenceObject(a->Owner);a->Process=IoGetRequestorProcess(irp);if(a->Process)ObReferenceObject(a->Process);a->OwnerLost=FALSE;a->Active=FALSE;++a->Generation;TitanAuthorizationInitialize(&a->Authorization,TITAN_AUTHORIZATION_USER_BACKED_DISK);}
 if(header.command!=UD_STATUS&&(a->Owner!=file||a->Process!=IoGetRequestorProcess(irp)||a->OwnerLost)){KeReleaseSpinLock(&a->Lock,old);status=STATUS_SHARING_VIOLATION;goto end;}
 KeReleaseSpinLock(&a->Lock,old);
 if(header.command==UD_CHALLENGE){TITAN_AUTHORIZATION_CHALLENGE challenge;status=TitanAuthorizationChallenge(&a->Authorization,&challenge);if(NT_SUCCESS(status)){KeAcquireSpinLock(&a->Lock,&old);if(a->Owner!=file||a->Process!=IoGetRequestorProcess(irp)||a->OwnerLost||(file->Flags&FO_CLEANUP_COMPLETE))status=STATUS_FILE_CLOSED;KeReleaseSpinLock(&a->Lock,old);if(NT_SUCCESS(status)){RtlZeroMemory(m,sizeof(*m));RtlCopyMemory(m->data,&challenge,sizeof(challenge));m->length=sizeof(challenge);StorPortNotification(RequestTimerCall,a,Timer,100000u);}}}
 else if(header.command==UD_AUTHORIZE){TITAN_AUTHORIZATION_TOKEN token;if(header.length!=sizeof(token)){status=STATUS_INVALID_PARAMETER;goto end;}RtlCopyMemory(&token,m->data,sizeof(token));status=TitanAuthorizationAccept(&a->Authorization,&token);RtlSecureZeroMemory(&token,sizeof(token));if(NT_SUCCESS(status)){KeAcquireSpinLock(&a->Lock,&old);if(a->Owner!=file||a->Process!=IoGetRequestorProcess(irp)||a->OwnerLost||(file->Flags&FO_CLEANUP_COMPLETE))status=STATUS_FILE_CLOSED;KeReleaseSpinLock(&a->Lock,old);}}
 else if(header.command==UD_START){if(!UdValidCapacity(header.capacity)||header.readonly>1||header.id||header.offset||header.length||header.operation){status=STATUS_INVALID_PARAMETER;goto end;}if(!TitanAuthorizationAllowed(&a->Authorization)){status=STATUS_ACCESS_DENIED;goto end;}KeAcquireSpinLock(&a->Lock,&old);if(a->Active||a->Stats.pending)status=STATUS_DEVICE_BUSY;else if(a->OwnerLost||!a->Running||a->Owner!=file||a->Process!=IoGetRequestorProcess(irp)||(file->Flags&FO_CLEANUP_COMPLETE))status=STATUS_DEVICE_NOT_READY;else{a->Capacity=header.capacity;a->Readonly=(BOOLEAN)header.readonly;InterlockedExchange(&a->DepthConfigured,0);a->Active=TRUE;++a->Generation;notify=TRUE;status=STATUS_SUCCESS;}KeReleaseSpinLock(&a->Lock,old);}
 else if(header.command==UD_STOP){Drain(a,SRB_STATUS_NO_DEVICE);notify=TRUE;status=STATUS_SUCCESS;}
 else if(header.command==UD_STATUS){UD_STATISTICS stats;KeAcquireSpinLock(&a->Lock,&old);stats=a->Stats;stats.active=Live(a);stats.version=UD_VERSION;stats.size=sizeof(stats);{ULONGLONG now=KeQueryInterruptTime(),deadline=(ULONGLONG)a->Authorization.deadline;stats.remaining_100ns=a->Owner&&!a->OwnerLost&&deadline>now?deadline-now:0;}RtlZeroMemory(m,sizeof(*m));m->capacity=a->Capacity;m->generation=a->Generation;m->readonly=a->Readonly;m->operation=a->DepthConfigured?UD_PENDING:0;m->id=(ULONGLONG)(ULONG)a->DepthAttempts|((ULONGLONG)a->ConfigBits<<32);{SLOT* first=First(a);if(first){m->offset=first->Srb->SrbFlags;m->result=(ULONG)first->Srb->QueueTag|((ULONG)first->Srb->QueueAction<<8);}}KeReleaseSpinLock(&a->Lock,old);RtlCopyMemory(m->data,&stats,sizeof(stats));m->length=sizeof(stats);status=STATUS_SUCCESS;}
 else if(header.command==UD_POLL){KeAcquireSpinLock(&a->Lock,&old);if(!Live(a))status=STATUS_DEVICE_NOT_READY;else if(a->PollReserved&&a->PollEpoch==a->Generation)status=STATUS_DEVICE_BUSY;else{a->PollReserved=TRUE;a->PollEpoch=a->Generation;irp->Tail.Overlay.DriverContext[0]=(PVOID)(ULONG_PTR)a->Generation;pend=TRUE;status=STATUS_PENDING;}KeReleaseSpinLock(&a->Lock,old);}
 else if(header.command==UD_COMPLETE){PSCSI_REQUEST_BLOCK done=NULL;STOR_LOCK_HANDLE port={0};StorPortAcquireSpinLock(a,StartIoLock,NULL,&port);KeAcquireSpinLock(&a->Lock,&old);status=STATUS_NOT_FOUND;if(Live(a)&&header.generation==a->Generation)for(i=0;i<UD_PENDING;++i){SLOT* p=&a->Slots[i];if(p->Srb&&p->Issued&&p->Id==header.id){if(header.length!=(header.result?0:(p->Operation==UD_READ?p->Bytes:0))){status=STATUS_INVALID_BUFFER_SIZE;break;}done=p->Srb;if(header.result){Sense(done,SCSI_SENSE_MEDIUM_ERROR,p->Operation==UD_READ?0x11:0x0c);++a->Stats.failed;}else{if(p->Operation==UD_READ)RtlCopyMemory(done->DataBuffer,m->data,p->Bytes);done->DataTransferLength=p->Bytes;done->SrbStatus=SRB_STATUS_SUCCESS;done->ScsiStatus=SCSISTAT_GOOD;++a->Stats.completed;}p->Srb=NULL;--a->Stats.pending;status=STATUS_SUCCESS;break;}}KeReleaseSpinLock(&a->Lock,old);if(done)CompleteSrb(a,done);StorPortReleaseSpinLock(a,&port);if(done)Pump(a);}
end:
 if(oldFile)ObDereferenceObject(oldFile);if(oldProcess)ObDereferenceObject(oldProcess);
 if(NT_SUCCESS(status)&&!pend){if(header.command!=UD_STATUS&&header.command!=UD_CHALLENGE)RtlZeroMemory(m,sizeof(*m));m->magic=UD_MAGIC;m->version=UD_VERSION;m->command=header.command;}
 KeReleaseMutex(&a->Control,FALSE);KeLeaveCriticalRegion();
 if(notify)StorPortNotification(BusChangeDetected,a,0);
 if(pend){IoMarkIrpPending(irp);IoCsqInsertIrp(&a->Csq,irp,NULL);Pump(a);}else FinishService(a,irp,status,sizeof(*m));
}
_Use_decl_annotations_ VOID CompleteServices(PVOID extension){ADAPTER* a=extension;if(a->Initialized)Drain(a,SRB_STATUS_NO_DEVICE);}
_Use_decl_annotations_ ULONG FindAdapter(PVOID extension,PVOID context,PVOID bus,PVOID lower,PCHAR arguments,PPORT_CONFIGURATION_INFORMATION c,PBOOLEAN again){ADAPTER* a=extension;NTSTATUS status;UNREFERENCED_PARAMETER(context);UNREFERENCED_PARAMETER(bus);UNREFERENCED_PARAMETER(lower);UNREFERENCED_PARAMETER(arguments);*again=FALSE;if(InterlockedCompareExchange(&AdapterClaim,1,0))return SP_RETURN_NOT_FOUND;a->OwnsSlot=TRUE;KeInitializeSpinLock(&a->Lock);KeInitializeSpinLock(&a->ServiceLock);KeInitializeMutex(&a->Control,0);InitializeListHead(&a->Waiters);status=IoCsqInitialize(&a->Csq,CsqInsert,CsqRemove,CsqPeek,CsqAcquire,CsqRelease,CsqCancel);if(!NT_SUCCESS(status)){a->OwnsSlot=FALSE;InterlockedExchange(&AdapterClaim,0);return SP_RETURN_ERROR;}a->Initialized=TRUE;c->VirtualDevice=TRUE;c->NumberOfBuses=1;c->MaximumTransferLength=UD_TRANSFER;c->NumberOfPhysicalBreaks=UD_TRANSFER/PAGE_SIZE+1;c->AlignmentMask=0;c->CachesData=FALSE;c->MapBuffers=STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE;c->SynchronizationModel=StorSynchronizeFullDuplex;a->ConfigBits=(c->TaggedQueuing?1u:0u)|(c->MultipleRequestPerLu?2u:0u)|(c->AutoRequestSense?4u:0u);if(c->Length>=FIELD_OFFSET(PORT_CONFIGURATION_INFORMATION,InitialLunQueueDepth)+sizeof(c->InitialLunQueueDepth))a->ConfigBits|=min(c->InitialLunQueueDepth,65535u)<<16;return SP_RETURN_FOUND;}
_Use_decl_annotations_ BOOLEAN PassiveInitialize(PVOID extension){ADAPTER* a=extension;StorPortInitializeDpc(a,&a->StopDpc,StopDpc);a->DpcInitialized=TRUE;a->Running=TRUE;return TRUE;}
_Use_decl_annotations_ BOOLEAN Initialize(PVOID extension){return StorPortEnablePassiveInitialization(extension,PassiveInitialize);}
_Use_decl_annotations_ BOOLEAN ResetBus(PVOID extension,ULONG path){ADAPTER* a=extension;BOOLEAN schedule;UNREFERENCED_PARAMETER(path);schedule=InterlockedCompareExchange(&a->StopQueued,1,0)==0;/* Storport calls at DISPATCH with StartIoLock; all SRBs must complete now. */(VOID)DrainSrbs(a,SRB_STATUS_BUS_RESET,TRUE);if(schedule&&a->DpcInitialized)if(!StorPortIssueDpc(a,&a->StopDpc,NULL,NULL))InterlockedExchange(&a->StopQueued,0);return TRUE;}
_Use_decl_annotations_ SCSI_ADAPTER_CONTROL_STATUS AdapterControl(PVOID extension,SCSI_ADAPTER_CONTROL_TYPE type,PVOID parameters){ADAPTER* a=extension;if(type==ScsiQuerySupportedControlTypes){PSCSI_SUPPORTED_CONTROL_TYPE_LIST p=parameters;if(p&&p->MaxControlType>ScsiRestartAdapter){p->SupportedTypeList[ScsiQuerySupportedControlTypes]=TRUE;p->SupportedTypeList[ScsiStopAdapter]=TRUE;p->SupportedTypeList[ScsiRestartAdapter]=TRUE;}return ScsiAdapterControlSuccess;}if(type==ScsiStopAdapter){InterlockedExchange(&a->Running,FALSE);QueueStop(a);return ScsiAdapterControlSuccess;}if(type==ScsiRestartAdapter){InterlockedExchange(&a->Active,FALSE);InterlockedExchange(&a->OwnerLost,TRUE);InterlockedExchange(&a->Running,TRUE);return ScsiAdapterControlSuccess;}return ScsiAdapterControlUnsuccessful;}
_Use_decl_annotations_ VOID FreeAdapter(PVOID extension){ADAPTER* a=extension;a->Removed=TRUE;a->Running=FALSE;if(a->Initialized){Drain(a,SRB_STATUS_NO_DEVICE);StorPortNotification(RequestTimerCall,a,Timer,0);}/* Drain retires both referenced objects under the ownership epoch lock. */if(a->OwnsSlot){a->OwnsSlot=FALSE;InterlockedExchange(&AdapterClaim,0);}}
_Use_decl_annotations_ NTSTATUS DriverEntry(PDRIVER_OBJECT object,PUNICODE_STRING path){HW_INITIALIZATION_DATA init={0};init.HwInitializationDataSize=sizeof(init);init.AdapterInterfaceType=Internal;init.HwInitialize=Initialize;init.HwFindAdapter=(PVOID)FindAdapter;init.HwStartIo=StartIo;init.HwResetBus=ResetBus;init.HwAdapterControl=AdapterControl;init.HwFreeAdapterResources=FreeAdapter;init.HwProcessServiceRequest=ServiceRequest;init.HwCompleteServiceIrp=CompleteServices;init.DeviceExtensionSize=sizeof(ADAPTER);init.MapBuffers=STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE;init.TaggedQueuing=TRUE;init.AutoRequestSense=TRUE;init.MultipleRequestPerLu=TRUE;init.FeatureSupport=STOR_FEATURE_VIRTUAL_MINIPORT;init.SrbTypeFlags=SRB_TYPE_FLAG_SCSI_REQUEST_BLOCK;init.AddressTypeFlags=ADDRESS_TYPE_FLAG_BTL8;return (NTSTATUS)StorPortInitialize(object,path,&init,NULL);}
