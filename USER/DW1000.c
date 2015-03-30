#include "stm32f10x.h"
#include "SPI.h"
#include "DW1000.h"
#include "USART.h"
#include "math.h"
#include "delay.h"

u8 mac[8];
u8 toggle = 1;
const u16 PANIDS[2] = {0x8974, 0x1074};
const u8 broadcast_addr[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Common
u8 Sequence_Number=0x00;
u32 Tx_stp_L;
u8 Tx_stp_H;
u32 Rx_stp_L;
u8 Rx_stp_H;

u32 Tx_stp_LT[3];
u8 Tx_stp_HT[3];
u32 Rx_stp_LT[3];
u8 Rx_stp_HT[3];

u8 Tx_Buff[128];
u8 Rx_Buff[128];
u32 LS_DATA[3];
u32 u32_diff;

extern u8 distance_flag;

u32 time_offset=0; //电磁波传播时间调整
u8 speed_offset=0; //电磁波传播速度调整
double distance[3];

u16 std_noise;
u16 fp_ampl1;
u16 fp_ampl2;
u16 fp_ampl3;
u16 cir_mxg;
u16 rxpacc;
double fppl;
double rxl;

/*
DW1000初始化
*/

void DW1000_init(void)
{
	u32 tmp;
	////////////////////工作模式配置////////////////////////
	//AGC_TUNE1 ：设置为16 MHz PRF
	tmp=0x00008870;
	Write_DW1000(0x23,0x04,(u8 *)(&tmp),2);
	//AGC_TUNE2 ：不知道干啥用，技术手册明确规定要写0x2502A907
	tmp=0x2502A907;
	Write_DW1000(0x23,0x0C,(u8 *)(&tmp),4);
	//DRX_TUNE2：配置为PAC size 8，16 MHz PRF
	tmp=0x311A002D;
	Write_DW1000(0x27,0x08,(u8 *)(&tmp),4);
	//NSTDEV ：LDE多径干扰消除算法的相关配置
	tmp=0x0000006D;
	Write_DW1000(0x2E,0x0806,(u8 *)(&tmp),1);
	//LDE_CFG2 ：将LDE算法配置为适应16MHz PRF环境
	tmp=0x00001607;
	Write_DW1000(0x2E,0x1806,(u8 *)(&tmp),2);
	//TX_POWER ：将发送功率配置为16 MHz,智能功率调整模式
	tmp=0x0E082848;
	Write_DW1000(0x1E,0x00,(u8 *)(&tmp),4);
	//RF_TXCTRL ：选择发送通道5
	tmp=0x001E3FE0;
	Write_DW1000(0x28,0x0C,(u8 *)(&tmp),4);
	//TC_PGDELAY ：脉冲产生延时设置为适应频道5
	tmp=0x000000C0;
	Write_DW1000(0x2A,0x0B,(u8 *)(&tmp),1);
	//FS_PLLTUNE ：PPL设置为适应频道5
	tmp=0x000000A6;
	Write_DW1000(0x2B,0x0B,(u8 *)(&tmp),1);

	mac[0] = 0xff;
	mac[1] = 0xff;
	mac[2] = 0xff;
	mac[3] = 0xff;
	mac[4] = 0xff;
	mac[5] = 0xff;
	mac[6] = 0xff;
	#ifdef TX
	mac[7] = 0xf0;
	#endif
	#ifdef RX1
	mac[7] = 0xf1;
	#endif
	#ifdef RX2
	mac[7] = 0xf2;
	#endif
	#ifdef RX3
	mac[7] = 0xf3;
	#endif
	set_MAC(mac);
	//no auto ack Frame Filter
	tmp=0x200011FD;
	// 0010 0000 0000 0001 0000 0111 1101
	Write_DW1000(0x04,0x00,(u8 *)(&tmp),4);
	// test pin SYNC：用于测试的LED灯引脚初始化，SYNC引脚禁用
	tmp=0x00101540;
	Write_DW1000(0x26,0x00,(u8 *)(&tmp),2);
	tmp=0x01;
	Write_DW1000(0x36,0x28,(u8 *)(&tmp),1);
	// interrupt   ：中断功能选择（只开启收发成功中断）
	tmp=0x00006080;
	Write_DW1000(0x0E,0x00,(u8 *)(&tmp),2);
	// ack等待
	tmp=3;
	Write_DW1000(0x1A,0x03,(u8 *)(&tmp),1);
	load_LDE();
	printf("定位芯片配置\t\t完成\r\n");
}

/*
申请定位
*/
void Location_polling(void)
{
	u16 tmp;
	// Tx_Buff[0]=0b10000010; // only DST PANID
	// Tx_Buff[1]=0b00110111;
	Tx_Buff[0] = 0x82;
	Tx_Buff[1] = 0x37;
	Tx_Buff[2] = Sequence_Number++;
	//SN end
	Tx_Buff[4] = 0xFF;
	Tx_Buff[3] = 0xFF;
	//DST PAN end
	Tx_Buff[6]=broadcast_addr[0];
	Tx_Buff[7]=broadcast_addr[1];
	Tx_Buff[8]=broadcast_addr[2];
	Tx_Buff[9]=broadcast_addr[3];
	Tx_Buff[10]=broadcast_addr[4];
	Tx_Buff[11]=broadcast_addr[5];
	Tx_Buff[12]=broadcast_addr[6];
	Tx_Buff[13]=broadcast_addr[7];
	//DST MAC end
	Tx_Buff[14]=mac[0];
	Tx_Buff[15]=mac[1];
	Tx_Buff[16]=mac[2];
	Tx_Buff[17]=mac[3];
	Tx_Buff[18]=mac[4];
	Tx_Buff[19]=mac[5];
	Tx_Buff[20]=mac[6];
	Tx_Buff[21]=mac[7];
	//SRC MAC end
	//NO AUX
	//Payload begin
	Tx_Buff[22] = 0x00; // 0x00 = LS Req
	Tx_Buff[23] = 0xFF;
	tmp = 23;
	raw_write(Tx_Buff, &tmp);
	distance_flag = SENT_LS_REQ;
}
/*
计算距离信息(单位：cm)并串口输出
*/
void distance_measurement(int n)
{
	double double_diff;
	if(Tx_stp_H == Rx_stp_HT[n])
	{
		double_diff = 1.0* (Rx_stp_HT[n] - Tx_stp_L);
	}
	else if(Tx_stp_H < Rx_stp_HT[n])
	{
		double_diff = 1.0*((Rx_stp_HT[n] - Tx_stp_H)*0xFFFFFFFF + Rx_stp_HT[n] - Tx_stp_L);
	}
	else
	{
		double_diff = 1.0*((0xFF - Tx_stp_H + Rx_stp_HT[n] +1)*0xFFFFFFFF + Rx_stp_HT[n] - Tx_stp_L);
	}

	double_diff = double_diff - 1.0*LS_DATA[n];

	distance[n] = 15.65*double_diff/1000000000000/2*_WAVE_SPEED*(1.0-0.01*speed_offset);
	printf("测定距离Node1\t\t%.2lf米\r\n\n\n",distance[0]);
	printf("测定距离Node2\t\t%.2lf米\r\n\n\n",distance[1]);
	printf("测定距离Node3\t\t%.2lf米\r\n\n\n",distance[2]);
	printf("\r\n=====================================\r\n");
}

/*
无线质量数据
*/
void quality_measurement(void)
{
	rxpacc>>=4;

	//抗噪声品质判定
	if((fp_ampl2/std_noise)>=2)
	{
		//printf("抗噪声品质\t\t良好\r\n");
	}
	else
	{
		//printf("抗噪声品质\t\t异常\r\n");
	}
	//LOS判定
	fppl=10.0*log((fp_ampl1^2+fp_ampl2^2+fp_ampl3^2)/(rxpacc^2))-115.72;
	rxl=10.0*log(cir_mxg*(2^17)/(rxpacc^2))-115.72;
	if((fppl-rxl)>=10.0*log(0.25))
	{
		//printf("LOS判定\t\t\tLOS\r\n");
	}
	else
	{
		//printf("LOS判定\t\t\tNLOS\r\n");
	}
}


/*
打开接收模式
*/
void RX_mode_enable(void)
{
	u8 tmp;
	// load_LDE(); // lucus: why here?
	tmp=0x01;
	Write_DW1000(0x0D,0x01,&tmp,1);
}
/*
返回IDLE状态
*/
void to_IDLE(void)
{
	u8 tmp;
	tmp=0x40;
	Write_DW1000(0x0D,0x00,&tmp,1);
}

void raw_write(u8* tx_buff, u16* size)
{
	u8 full_size;
	to_IDLE();
	Write_DW1000(0x09, 0x00, tx_buff, *size);
	full_size = (u8)(*size + 2);
	Write_DW1000(0x08, 0x00, &full_size, 1);
	// sent and wait
	sent_and_wait();
}

void raw_read(u8* rx_buff, u16* size)
{
	to_IDLE();
	Read_DW1000(0x10, 0x00, (u8 *)(size), 1);
	*size -= 2;
	Read_DW1000(0x11, 0x00, rx_buff, *size);
	RX_mode_enable();
}

void load_LDE(void)
{
	u16 tmp=0x00;
	Write_DW1000(0x36,0x06,(u8 *)(&tmp),1);
	//ROM TO RAM
	//LDE LOAD=1
	tmp=0x8000;
	Write_DW1000(0x2D,0x06,(u8 *)(&tmp),2);
	Delay(20);
	tmp=0x0002;
	Write_DW1000(0x36,0x06,(u8 *)(&tmp),1);
}

void sent_and_wait(void)
{
	u8 tmp = 0x82;
	Write_DW1000(0x0D, 0x00, &tmp, 1);
}

void set_MAC(u8* mac)
{
	Write_DW1000(0x01, 0x00, mac, 8);
}

void read_status(u32 *status)
{
	Read_DW1000(0x0F,0x00,(u8 *)(status),4);
}

void data_response(u8 *src, u8 *dst)
{
	u16 tmp;
	Read_DW1000(0x17,0x00,(u8 *)(&Tx_stp_L),4);
	Read_DW1000(0x15,0x09,(u8 *)(&Rx_stp_L),4);
	Read_DW1000(0x17,0x04,&Tx_stp_H,1);
	Read_DW1000(0x15,0x0d,&Rx_stp_H,1);
	printf("%8x\r\n",Rx_stp_L);
	printf("%2x\r\n",Rx_stp_H);
	printf("%8x\r\n",Tx_stp_L);
	printf("%2x\r\n",Tx_stp_H);
	if(Tx_stp_H==Rx_stp_H)
	{
		u32_diff=(Tx_stp_L-Rx_stp_L);
	}
	else if(Rx_stp_H<Tx_stp_H)
	{
			u32_diff=((Tx_stp_H-Rx_stp_H)*0xFFFFFFFF+Tx_stp_L-Rx_stp_L);
	}
	else
	{
		u32_diff=((0xFF-Rx_stp_H+Tx_stp_H+1)*0xFFFFFFFF+Tx_stp_L-Rx_stp_L);
	}
	
	printf("%08x\r\n", u32_diff);
	// Tx_Buff[0]=0b10000010; // only DST PANID
	// Tx_Buff[1]=0b00110111;
	Tx_Buff[0] = 0x82;
	Tx_Buff[1] = 0x37;
	// 0100 0001 1000 1000
	Tx_Buff[2] = Sequence_Number++;
	//SN end
	Tx_Buff[4] = 0xFF;
	Tx_Buff[3] = 0xFF;
	//DST PAN end
	Tx_Buff[6]=dst[0];
	Tx_Buff[7]=dst[1];
	Tx_Buff[8]=dst[2];
	Tx_Buff[9]=dst[3];
	Tx_Buff[10]=dst[4];
	Tx_Buff[11]=dst[5];
	Tx_Buff[12]=dst[6];
	Tx_Buff[13]=dst[7];
	//DST MAC end
	Tx_Buff[14]=src[0];
	Tx_Buff[15]=src[1];
	Tx_Buff[16]=src[2];
	Tx_Buff[17]=src[3];
	Tx_Buff[18]=src[4];
	Tx_Buff[19]=src[5];
	Tx_Buff[20]=src[6];
	Tx_Buff[21]=src[7];
	//SRC MAC end
	Tx_Buff[22] = 0x02; // 0x02 = LS DATA
	Tx_Buff[23] = (u8)u32_diff;
	u32_diff >>= 8;
	Tx_Buff[24] = (u8)u32_diff;
	u32_diff >>= 8;
	Tx_Buff[25] = (u8)u32_diff;
	u32_diff >>= 8;
	Tx_Buff[26] = (u8)u32_diff;
	Tx_Buff[27] = 0x01;
	tmp = 27;
	raw_write(Tx_Buff, &tmp);
}

void parse_rx(u8 *rx_buff, u16 size, u8 **src, u8 **dst, u8 **payload, u16 *pl_size)
{
	u16 n = 24;
	if (rx_buff[0]&0x02 == 0x02) {
		// PANID compress
		n -= 2;
	}
	
	if (rx_buff[1]&0x30 == 0x00) {
		n -= 8;
	} else if (rx_buff[1]&0x30 == 0x20) {
		n -= 6;
	}
	
	if (rx_buff[1]&0x03 == 0x00) {
		n -= 8;
	} else if (rx_buff[1]&0x03 == 0x02) {
		n -= 6;
	}
	
	*src = &(rx_buff[n-8]);
	*dst = &(rx_buff[n-16]);
	*payload = &(rx_buff[n]);
	*pl_size = size - n;
}

void send_LS_ACK(u8 *src, u8 *dst)
{
	u16 tmp;
	// Tx_Buff[0]=0b10000010; // only DST PANID
	// Tx_Buff[1]=0b00110111;
	Tx_Buff[0] = 0x82;
	Tx_Buff[1] = 0x37;
	// 0100 0001 1000 1000
	Tx_Buff[2] = Sequence_Number++;
	//SN end
	Tx_Buff[4] = 0xFF;
	Tx_Buff[3] = 0xFF;
	//DST PAN end
	Tx_Buff[6]=dst[0];
	Tx_Buff[7]=dst[1];
	Tx_Buff[8]=dst[2];
	Tx_Buff[9]=dst[3];
	Tx_Buff[10]=dst[4];
	Tx_Buff[11]=dst[5];
	Tx_Buff[12]=dst[6];
	Tx_Buff[13]=dst[7];
	//DST MAC end
	Tx_Buff[14]=src[0];
	Tx_Buff[15]=src[1];
	Tx_Buff[16]=src[2];
	Tx_Buff[17]=src[3];
	Tx_Buff[18]=src[4];
	Tx_Buff[19]=src[5];
	Tx_Buff[20]=src[6];
	Tx_Buff[21]=src[7];
	//SRC MAC end
	Tx_Buff[22] = 0x01; // 0x01 = LS ACK
	Tx_Buff[23] = 0x01;
	tmp = 23;
	raw_write(Tx_Buff, &tmp);
}