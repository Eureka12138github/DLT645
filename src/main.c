/**
 * @file    main.c
 * @brief   DL/T 645-2007 电能表通信协议 —— 命令行工具入口
 *
 * 本文件是协议库（dlt645_api_07.c / dlt645_api_07.h）的演示外壳，提供三类功能：
 *   1. 规约查询：按 4 字节规约标识（DI）打印数据项的类型、长度、格式与名称；
 *   2. 报文解析：把一串十六进制字节解析成 645-2007 帧（unpack）；
 *   3. 报文组帧：按交互输入拼出 645-2007 帧（pack）。
 *
 * 学习主线——645-2007 帧结构：
 *   0x68 | A0~A5(BCD，低字节在前) | 0x68 | C | L | DATA(每字节+0x33) | CS | 0x16
 *   C  ：控制码，bit7=传送方向(0 主站→从站, 1 从站→主站)，bit6=异常标志，bit0~4=功能码
 *   L  ：数据域字节数（含 4 字节 DI）
 *   CS ：从第一个 0x68 起累加到 DATA 末字节，取低 8 位
 *
 * @note 源文件为 UTF-8；构建时加 -fexec-charset=GBK，让中文字符串以 GBK 字节写入 exe，
 *       否则 Windows 控制台（GBK 代码页）会显示乱码。
 * @note 学习阶段协议库逻辑保持原样，本文件只承担命令行演示职责。
 */

#include <stdio.h>
#include <stdlib.h>              /* strtol：十六进制字符串 → 整数 */
#include <string.h>              /* strcmp / memcpy */
#include <unistd.h>              /* POSIX 声明（MinGW 提供 getopt 相关） */
#include <getopt.h>              /* getopt_long：长选项解析 */
#include "dlt645_api_07.h"       /* 协议库对外接口、数据结构与枚举 */


/* 调试用计数：仅被注释掉的调试打印使用，保留以兼容原程序 */
int g_num = 0;

/* ---------------- 本文件内部函数声明 ---------------- */
void test_get_ruler_info();                         /* 自测：批量打印规约信息（当前未被调用） */
void show_packet(int buflen, char* buf);            /* 以十六进制打印一段帧数据 */
int  show_ruler_info(unsigned long inRulerID);      /* 打印一条规约（DI）的详细信息 */
void pack_d07_frame(unsigned long inRulerID);       /* 交互式组帧 */
void unpack_d07_frame_test(int argc, char*argv[]);  /* 解析命令行给出的一帧报文 */
int  show_all_valid_ruler_info();                   /* 全表扫描，统计有效规约条数 */
void test_all_ruler_d07_meter();                    /* 预留：与真实国标表联调（空实现） */
	






/**
 * @brief  打开/关闭协议库内部的调试输出
 * @param  show  D07_ON 打开，D07_OFF 关闭
 *
 * 开关本质是协议库的全局变量 g_need_pack_07（定义于 dlt645_api_07.c）：
 *   - 组帧方向（U2F）打开后，转换函数会 scanf 提示操作者输入数据；
 *   - 解析方向（F2U）打开后，转换函数把结果格式化写入 g_out_data_07。
 * 因此 main.c 在调用转换/解析函数前后成对调用本函数。
 */
void debug_switch(int show)
{
	 g_need_pack_07 = show;
}
	
/* 命令行帮助文本：-h / --help 时整段打印 */
char helpinfo[] = 
	{
		"dlt645 2007 ruler anlaysis program.\n"
		"usage:d07 [-i]\n"
		"--help,-h: help info\n"
		"--info,-i, get ruler infor\n"
		"--all, show all valid ruler infor\n"
	};

/**
 * @brief  程序入口：解析命令行参数并分发到对应功能
 * @param  argc 参数个数
 * @param  argv 参数数组
 * @return 0 正常退出；-1 参数非法
 *
 * 支持的选项：
 *   短选项 -v 版本 / -h 帮助 / -i <DI> 查规约 / -p <DI> 组帧 / -u <帧字节...> 解析帧
 *   长选项 --version / --info / --pack / --unpack / --help / --all
 */
int main(int argc, char *argv[])
{

	int opt = 0;              /* getopt_long 返回的选项字符 */
	int longIndex = 0;        /* 命中的长选项在 longOpts 中的下标 */
	int ret = 0;              /* 函数返回值暂存 */
	int num = 0;              /* --all 统计到的有效规约条数 */
	/* 短选项串：末尾带冒号表示该选项必须跟一个参数 */
	char *strOpt = "vahi:p:u:";
	/* 长选项表：{长名, 是否带参数, flag 指针, 返回值}
	 *  - flag 为 NULL 时，getopt_long 直接返回“返回值”字段；
	 *  - {"all", 0} 用返回码 0 作为自定义标记，再靠 longOpts[longIndex].name 分辨是哪个长选项；
	 *  - {"output", 'o'} 只做了声明，主循环未实现该分支（原版遗留），传 -o 会落到 default。 */
	struct option longOpts[] = 
	{
		{"version",		no_argument, 		NULL, 	'v'},
		{"output",		required_argument,	NULL, 	'o'},
		{"info",		required_argument,	NULL,	'i'},
		{"pack",		required_argument,	NULL,	'p'},
		{"unpack",		required_argument,	NULL,	'u'},
		{"help",		no_argument,		NULL,	'h'},
		{"all",			no_argument,		NULL,	0},
		{NULL,			no_argument,		NULL,	0}
	};

	unsigned long ulRulerID = 0;
	
	while((opt = getopt_long(argc, argv,strOpt, longOpts, &longIndex)) != -1)
	{
		switch(opt)
		{
			case 'u':
				/* 其后连续的十六进制字节即一帧报文；解析函数从 argv[2] 开始取，
				 * 依赖 GNU getopt 把非选项参数重排到末尾的特性 */
				unpack_d07_frame_test(argc, argv);
				break;
				
			case 'v':
				printf("**************************************\n");
				printf("     name :\tDL/T645 2007 tools\n");
				printf("  version :\t1.0\n");
				printf("   author :\tliming\n");
				printf("    email :\tvliming@126.com\n");
				printf("**************************************\n");
				break;
					
				
			case 'i':
				/* 16 进制字符串 → 整数 DI，如 "00000000" → 0x00000000 */
				ulRulerID = (unsigned long)strtol(optarg,NULL,16); // 将字符串十六进制数转换为整数
				ret = show_ruler_info(ulRulerID);
				if(ret != E_D07_OK)
				{
					printf("不存在的规约类型\n\n");
				}
				break;

			case 'p':
				/* 同上：把 DI 转成整数后进入交互式组帧流程 */
				ulRulerID = (unsigned long)strtol(optarg,NULL,16); // 将字符串十六进制数转换为整数
				pack_d07_frame(ulRulerID);
				break;
	
			case 'h':
				printf("%s\n",helpinfo);
				break;
								
			
				

			case 0:
				/* 返回码 0 = 无返回值的自定义长选项（本表只有 --all） */
				if(!strcmp("all", longOpts[longIndex].name))
				{	
					num = show_all_valid_ruler_info();
					printf("一共%d条有效的规约\n", num);
				}
				break;
				
			default:
				/* 未知选项或缺少参数 */
				printf("Your command line parameters are not right!\n");  
				return -1;
		}	
		
	}

	//test_get_ruler_info();

	return 0;
}




/**
 * @brief  遍历规约表可能的取值区间，打印并统计全部有效规约
 * @return 有效规约条数
 *
 * 逐个整数 DI 调用 get_d07_ruler_info 判断是否存在（命中即打印），
 * 上界 0x06100000 取自原程序，全量扫描耗时很长（README 中 --all 已标注为慢命令）。
 */
int show_all_valid_ruler_info()
{
	int rulerCount = 0;
	int i = 0;
	int ret = 0;
	unsigned long ulRulerID = 0;
	/* 上界 0x06100000：规约表按下标顺序排列，超出该范围不会再有有效项 */
	for(i = 0; i < 0x06100000; i++)
	{
		ulRulerID = i;
		ret = show_ruler_info(ulRulerID);	
		if(ret == E_D07_OK)
		{	
			
			rulerCount++;
			printf("%d:\n",rulerCount);
		}
	}

	return rulerCount;
}

/**
 * @brief  解析一帧 645-2007 报文并打印结果
 * @param  argc 参数个数
 * @param  argv 参数数组；帧字节从 argv[2] 起，每个参数是一个十六进制字节
 *
 * 流程：argv[2..] 逐字节 strtol → unpack_d07_frame → 打印规约信息/帧信息/数据域/说明
 * @note  依赖 getopt 的参数重排，用法示例：
 *        dlt645_cli.exe -u 68 01 00 00 00 00 00 68 91 08 33 33 33 33 89 67 45 33 9E 16
 */
void unpack_d07_frame_test(int argc, char*argv[])
{
	printf("argc = %d \n",argc);
	
	int i,j = 0;
	int ret = 0;
	char buf[512] = {0};
	int len = 0;
	S_D07_UNPACK stUnPack = {0};

	//printf("buffer = ");
	/* 从 argv[2] 开始逐字节读取：strtol(...,16) 兼容 "68" 与 "0x68" 两种写法；
	 * 原程序未校验合法性，非十六进制字符会得到 0 */
	for(i = 2; i < argc; i++)
	{
		//printf("%s", argv[i]);
		buf[j++] = strtol(argv[i], NULL,16);
	}

	/* len = 实际读入的字节数 */
	len = j;
	/* 打开调试开关：解析结果会写入 g_out_data_07 */
	debug_switch(D07_ON);
	ret = unpack_d07_frame(buf, len, &stUnPack);
	debug_switch(D07_OFF);
	if(ret != E_D07_OK)
	{
		printf("\n  解析出错! (error = %d : ",ret);
		/* 按协议库返回的错误码翻译成中文提示 */
		switch(ret)
		{
			case E_D07_ERRO_FRAME_UNCOMP:
				printf("不完整的帧数据)\n");
				break;

			case E_D07_ERRO_FRAME_0x68:
				printf("起始符 0x68 的位置不对)\n");
				break;
				
			case E_D07_ERRO_FRAME_CHECK_SUM:
				printf("测试帧检验和不对)\n");
				break;
				
			case E_D07_ERRO_FRAME_END_0x16:
				printf("测试帧结束符 0x16 不对)\n");
				break;
			default:
				break;
		}
		
		printf("\n");
		return;
	}
	
	// 显示结果
	printf("/------------------unpack result-------------------\\\n");
	printf("\n<1> - ruler info:\n");
	show_ruler_info(stUnPack.ruler_id);
	printf("\n<2> - frame info:\n");
	printf(" ctrl_code\t=  0x%02X\n", stUnPack.ctrl_c);
	printf(" 0xFE_num\t=  %d\n", stUnPack.lead_num);
	printf(" data_len\t=  %d\n", stUnPack.data_len);
	printf(" frame_len\t=  %d\n", stUnPack.frame_len);
	//stUnPack.address[12] = 0;
	printf(" address\t=  %s\n", stUnPack.address);
	printf("\n<3> - data fields \n");
	/* flag 说明解析结果是否可用：OK=正常应答，ERROR_OK=异常应答 */
	if(stUnPack.flag == E_D07_UNPD_FLG_OK)
	{

		printf("正确的回答:\n");
	}
	else if(stUnPack.flag == E_D07_UNPD_FLG_ERROR_OK)
	{

		printf("异常的回答:\n");
	}
	printf("%s\n", g_out_data_07);

	printf("\n<4> - remark:\n\n\t");
	/* 用控制码解出的方向 + 功能码，拼出这帧报文的中文含义 */
	if(stUnPack.ctrl_s.direct == E_D07_CTRL_DIR_M2S)
	{
		printf("主站到从站的");
		if(E_D07_CTRL_READ_DATA == stUnPack.ctrl_s.funcode)
		{
			printf("读数据");
		}
		else if(E_D07_CTRL_WRITE_DATA == stUnPack.ctrl_s.funcode)
		{
			printf("写数据");
		}
		printf("请求\n\n");
	}
	else
	{		
		printf("从站到主站的");
		if(E_D07_CTRL_READ_DATA == stUnPack.ctrl_s.funcode)
		{
			printf("对读数据请求的响应");
		}
		else if(E_D07_CTRL_WRITE_DATA == stUnPack.ctrl_s.funcode)
		{
			printf("对写数据成功的响应");
		}
		printf("\n\n");
	}



	printf("\\------------------unpack result-------------------/\n");
}




/**
 * @brief  交互式组帧：按提示输入地址与方向，调用协议库封帧并打印结果
 * @param  inRulerID 4 字节规约标识（DI），决定数据格式与数据域长度
 *
 * 交互步骤：
 *   1) 输入地址（≤12 位）；2) 选择方向（0 主站→从站 / 1 从站→主站）
 *   - 主站→从站：只读项直接组“读数据请求”；读写项再选“读/写”，选写时由转换函数提示输入数据；
 *   - 从站→主站：组“正常应答”，数据同样由转换函数提示输入（需调试开关打开）。
 * @note  帧的公共部分（0x68/地址/0x68/C/L/DI/数据+0x33/CS/0x16）由协议库完成，
 *        本函数只负责准备 S_D07_PACK_FRAME（地址、控制码、数据、数据域长度）。
 */
void pack_d07_frame(unsigned long inRulerID)
{
	int num = 1;

	printf("0x%02lX\n", inRulerID);
	S_D07_RULER_INFO info = {0};
	int ret = get_d07_ruler_info(inRulerID, &info);
	/* 查不到规约就无法确定数据格式与数据域长度，直接退出 */
	if(ret != E_D07_OK)	
	{
		printf("不存在的规规约类型\n\n");
		return ;
	}

	unsigned char ucCtrl = 0;
	S_D07_CTRL_CODE stCtrl = {0};
	int dir = 0;
	
	/* 地址以字符串输入（最长 12 位十六进制数字串），协议库内部用 d07_str2bcd
	 * 转成 6 字节 BCD，且低字节在前（645 地址域的发送顺序） */
	char addr[64] = {0}; // 地址
	S_D07_PACK_FRAME pack_frame = {0};
	int length = 0;
	char buffer[256] = {0};
	char data[256] = {0};
	char user[256] = {0};
	F_D07_RULER_TRANS func = NULL;
	printf("\n-------------------------------------------------------------------");
	printf("\n%d - 输入地址(小于等于12位):\n\n\taddress <<  ", num++);
	scanf("%s",addr);

	// 方向
	printf("\n%d - 传输方向:   主站到从站(0)  从站到主站(1)\n\n\tdirect << ", num++);
	scanf("%d", &dir);
	if(dir != 0 && dir != 1)
	{
		printf("\n输入错误\n");
		return;
	}

	stCtrl.direct = (E_D07_CTRL_DIR)dir;   /* 方向最终写入控制码 C 的 bit7 */

	// 主站到从站
	if(E_D07_CTRL_DIR_M2S == dir)
	{
		/* 读数据请求：数据域只有 4 字节 DI，所以 data_len = 4 且不带其它数据 */
		if(info.rdwr == E_D07_RDWR_READ_ONLY)
		{
			stCtrl.funcode = E_D07_CTRL_READ_DATA;
			pack_frame.data_len = 4;
			pack_frame.data = NULL;
		}
		else if(info.rdwr == E_D07_RDWR_READ_WRITE)
		{
			//功能码
			printf("\n%d - 功能码:  读数据(0) 写数据(1)\n operate << ",num++);
			scanf("%d", &dir);
			if(dir == 0)
			{
				stCtrl.funcode = E_D07_CTRL_READ_DATA;
				pack_frame.data_len = 4;
				pack_frame.data = NULL;
			}
			else if(1 == dir)
			{
				/* 写数据：数据域 = 4 字节 DI + info.len 字节数据；调试开关打开后，
				 * 转换函数会提示逐项输入数据（原程序把交互放到了协议库内部） */
				debug_switch(D07_ON);
				stCtrl.funcode = E_D07_CTRL_WRITE_DATA;
				pack_frame.data_len = info.len + 4;
				func = info.func;
				func(E_D07_TRANS_U2F,user, data);
				pack_frame.data = data;
				debug_switch(D07_OFF);
			}
			else
			{
				printf("\n输入错误\n");
				return;
			}
		}
	}
	else	// 从站到主站
	{
		printf("\n%d - 数据域内容:\n\n",num++);
		// 正确的应答
		/* 从站→主站：组正常应答帧，控制码功能码取“读数据”；数据域 = 4 字节 DI +
		 * info.len 字节数据，具体数值由转换函数在调试开关下提示输入 */
		func = info.func;
		debug_switch(D07_ON);
		func(E_D07_TRANS_U2F, user, data);
		debug_switch(D07_OFF);
		stCtrl.funcode = E_D07_CTRL_READ_DATA;
		pack_frame.data = data;
		pack_frame.data_len = info.len + 4;
		// 异常的应答
	}

	/* 控制码结构体 → 单字节 C（方向/异常标志/功能码） */
	ret = trans_d07ctrl_struct2char(&ucCtrl, &stCtrl); //将结构封装成字节
	
	if(ret != E_D07_OK)
	{
		printf("\ntrans_d07ctrl_struct2char failed %d\n", ret);
		return;
	}
	
	pack_frame.ruler_id = inRulerID;
	pack_frame.ctrl_code = ucCtrl;
    /* 地址以字符串形式带 13 字节（12 位 + 结束符），与 S_D07_PACK_FRAME.address[13] 对应 */
    memcpy(pack_frame.address,addr, 13 );

	/* 封帧：输出整帧字节流与总长度（含 0x68…0x16 全流程） */
	ret = pack_d07_frame_by_data(&pack_frame, buffer, &length);
	if(ret != E_D07_OK)
	{
		printf("\npack frame failed %d\n", ret);
		return;
	}

	printf("\n%d - ruler infor:\n\n", num++);
	show_ruler_info(inRulerID);
	printf(" ctrl\t=  0x%02X\n", ucCtrl);
	printf(" addr\t=  %s\n", addr);

	
	//显示帧内容
	printf("\n%d - frame:\n\n\t", num++);
	show_packet(length, buffer);   /* 十六进制打印整帧，便于与真实报文比对 */
	printf("\n\n-------------------------------------------------------------------\n");
	
}



/* 自测函数：批量打印一段 DI 区间的规约信息（当前未被调用，保留作调试用） */
void test_get_ruler_info()
{
	show_ruler_info(0);
	unsigned long ulRulerID = 0;

	for(ulRulerID = 0; ulRulerID <= 0x00003F00; ulRulerID++)
	{
		show_ruler_info(ulRulerID);
	}

}




typedef struct 
{
	F_D07_RULER_TRANS func;
	char * funcName;

}funcInfo;

/**
 * @brief  打印一条规约（DI）的详细信息
 * @param  inRulerID 4 字节规约标识，如 0x00000000（组合有功总电能）
 * @return E_D07_OK 成功；E_D07_ERRO_UNKOWN_ID 规约不存在
 * @note   formats[] 与 funcs[] 都以枚举值 info.format 作下标，
 *         顺序必须与 dlt645_api_07.h 中 E_D07_RULER_FORMAT 的取值一一对应；
 *         一旦错位，显示的格式名/函数名会与实际不符。
 */
int show_ruler_info(unsigned long inRulerID)
{
	unsigned char ucDi3 = 0;
	unsigned char ucDi2 = 0;
	unsigned char ucDi1 = 0;
	unsigned char ucDi0 = 0;

	/* 把 DI 拆成 4 个字节：0x0400040B → [04][00][04][0B]（DI3 为最高字节） */
	ucDi0 = inRulerID & 0xFF;
	ucDi1 = (inRulerID >> 8) & 0xFF;
	ucDi2 = (inRulerID >> 16) & 0xFF;
	ucDi3 = (inRulerID >> 24) & 0xFF;
	S_D07_RULER_INFO info = {0};

	/* 数据格式名表：下标 = info.format（枚举 E_D07_RULER_FORMAT） */
	char* formats[] = { "E_D07_FMT_UNKOWN",
						"E_D07_FMT_XXXX",
						"E_D07_FMT_XXXXXX",
						"E_D07_FMT_XX_4",
						"E_D07_FMT_XX_6",
						"E_D07_FMT_XX_8",
						"E_D07_FMT_XX_10",
						"E_D07_FMT_XX_16",
						"E_D07_FMT_X_XXX",
						"E_D07_FMT_XX_XX",
						"E_D07_FMT_XX_XXXX",
						"E_D07_FMT_XX_XXXX_8",
						"E_D07_FMT_XXX_X",
						"E_D07_FMT_XXX_XXX",
						"E_D07_FMT_XXXXXX_XX",
						"E_D07_FMT_XXXXXXXX",
						"E_D07_FMT_XXXXXX_2",
						"E_D07_FMT_XXXXXX_6",
						"E_D07_FMT_NN",
						"E_D07_FMT_NNNN",
						"E_D07_FMT_NN_4",
						"E_D07_FMT_NN_6",
						"E_D07_FMT_NN_32",
						"E_D07_FMT_N_NNN",
						"E_D07_FMT_NN_NNNN",
						"E_D07_FMT_NNN_N",
						"E_D07_FMT_hhmmss",
						"E_D07_FMT_MMDDhhmm",
						"E_D07_FMT_MMDDNN_14",
						"E_D07_FMT_hhmmNN_14",
						"E_D07_FMT_DDhh",
						"E_D07_FMT_YYMMDDWW",
						"E_D07_FMT_YYMMDDNN",
						"E_D07_FMT_YYMMDDhhmm",
						"E_D07_FMT_YYMMDDhhmmNN",
						"E_D07_FMT_XX_XXXX_YYMMDDhhmmss",
						"E_D07_FMT_XX_XXXX_YYMMDDhhmmss_2",
						"E_D07_FMT_YYMMDDhhmmss_2_XX_XXXX_YYMMDDhhmm",
						"E_D07_FMT_XX_XXXX_YYMMDDhhmm",
						"E_D07_FMT_YYMMDDhhmmss_XXX_XXX_YYMMDDhhmmss",
						"E_D07_FMT_RECD_LOSS_LESS_VOLT",
						"E_D07_FMT_RECD_ANTI_PHASE",	
						"E_D07_FMT_RECD_UN_BALANCE",	
						"E_D07_FMT_RECD_LESS_MORE_LOSS_ELEC",
						"E_D07_FMT_RECD_VOLT_PASS_PERCENT",
						"E_D07_FMT_RECD_METER_PROG",
						"E_D07_FMT_RECD_METER_CLEAR",
						"E_D07_FMT_RECD_DEAMD_CLEAR",
						"E_D07_FMT_RECD_EVENT_CLEAR",
						"E_D07_FMT_RECD_ADJUST_TIME",
						"E_D07_FMT_RECD_PART_PROG",
						"E_D07_FMT_RECD_ZONE_PROG",
						"E_D07_FMT_RECD_WEEKDAY_PROG",
						"E_D07_FMT_RECD_HOLIDAY_PROG",
						"E_D07_FMT_RECD_POWER_PROG",
						"E_D07_FMT_RECD_PAYOFF_PROG",
						"E_D07_FMT_RECD_OPEN_METER_CAP",
						"E_D07_FMT_RECD_OPEN_BTN_BOX"
						};

	/* 转换函数表：下标 = info.format，元素 = 函数指针 + 函数名（名字仅用于显示）。
	 * 学习时以数组下标与枚举的对应关系为准，不要单看名字。 */
	funcInfo funcs[] = {{NULL, "unkown"},
						{trans_d07_data_XXXX ,"trans_d07_data_XXXX"},
						{trans_d07_data_XXXXXX ,"trans_d07_data_XXXXXX"},
						{trans_d07_data_XX_4,"trans_d07_data_XX_4"},
						{trans_d07_data_XX_6 ,"trans_d07_data_XX_6"},
						{trans_d07_data_XX_8,"trans_d07_data_XX_8"},
						{trans_d07_data_XX_10 ,"trans_d07_data_XX_10"},
						{trans_d07_data_XX_16 ,"trans_d07_data_XX_16"},
						{trans_d07_data_X_XXX ,"trans_d07_data_X_XXX"},
						{trans_d07_data_XX_XX ,"trans_d07_data_XX_XX"},
						{trans_d07_data_XX_XXXX,"trans_d07_data_XX_XXXX"},
						{trans_d07_data_XX_XXXX_8,"trans_d07_data_XX_XXXX_8"},
						{trans_d07_data_XXX_X ,"trans_d07_data_XXX_X"},
						{trans_d07_data_XXX_XXX ,"trans_d07_data_XXX_XXX"},
						{trans_d07_data_XXXXXX_XX ,"trans_d07_data_XXXXXX_XX"},
						{trans_d07_data_XXXXXXXX ,"trans_d07_data_XXXXXXXX"},
						{trans_d07_data_XXXXXX_2 ,"trans_d07_data_XXXXXX_2"},
						{trans_d07_data_XXXXXX_6 ,"trans_d07_data_XXXXXX_6"},
						{trans_d07_data_NN ,"trans_d07_data_NN"},
						{trans_d07_data_NNNN ,"trans_d07_data_NNNN"},
						{trans_d07_data_NN_4,"trans_d07_data_NN_4"},
						{trans_d07_data_NN_6,"trans_d07_data_NN_6"},
						{trans_d07_data_NN_32,"trans_d07_data_NN_32"},
						{trans_d07_data_N_NNN,"trans_d07_data_N_NNN"},
						{trans_d07_data_NN_NNNN,"trans_d07_data_NN_NNNN"},
						{trans_d07_data_NNN_N,"trans_d07_data_NNN_N"},
						{trans_d07_data_hhmmss ,"trans_d07_data_hhmmss"},
						{trans_d07_data_MMDDhhmm ,"trans_d07_data_MMDDhhmm"},
						{trans_d07_data_MMDDNN_14,"trans_d07_data_MMDDNN_14"},
						{trans_d07_data_hhmmNN_14,"trans_d07_data_hhmmNN_14"},
						{trans_d07_data_DDhh ,"trans_d07_data_DDhh"},
						{trans_d07_data_YYMMDDWW ,"trans_d07_data_YYMMDDWW"},
						{trans_d07_data_YYMMDDNN ,"trans_d07_data_YYMMDDNN"},
						{trans_d07_data_YYMMDDhhmm,"trans_d07_data_YYMMDDhhmm"},
						{trans_d07_data_YYMMDDhhmmNN,"trans_d07_data_YYMMDDhhmmNN"},
						{trans_d07_data_YYMMDDhhmmss,"trans_d07_data_YYMMDDhhmmss"},
						{trans_d07_data_YYMMDDhhmmss_2,"trans_d07_data_YYMMDDhhmmss_2"},
						{trans_d07_data_YYMMDDhhmmss_2_XX_XXXX_YYMMDDhhmm,"trans_d07_data_YYMMDDhhmmss_2_XX_XXXX_YYMMDDhhmm"},
						{trans_d07_data_XX_XXXX_YYMMDDhhmm ,"trans_d07_data_XX_XXXX_YYMMDDhhmm"},
						{trans_d07_data_YYMMDDhhmmss_XXX_XXX_YYMMDDhhmmss ,"trans_d07_data_YYMMDDhhmmss_XXX_XXX_YYMMDDhhmmss"},
						{trans_d07_data_recd_loss_less_volt ,"trans_d07_data_recd_loss_less_volt"},
						{trans_d07_data_recd_anti_phase ,"trans_d07_data_recd_anti_phase"},
						{trans_d07_data_recd_un_balance ,"trans_d07_data_recd_un_balance"},
						{trans_d07_data_recd_less_more_loss_elec ,"trans_d07_data_recd_less_more_loss_elec"},
						{trans_d07_data_recd_volt_pass_percent ,"trans_d07_data_recd_volt_pass_percent"},
						{trans_d07_data_recd_meter_prog ,"trans_d07_data_recd_meter_prog"},
						{trans_d07_data_recd_meter_clear ,"trans_d07_data_recd_meter_clear"},
						{trans_d07_data_recd_demand_clear ,"trans_d07_data_recd_demand_clear"},
						{trans_d07_data_recd_event_clear ,"trans_d07_data_recd_event_clear"},
						{trans_d07_data_recd_adjust_time ,"trans_d07_data_recd_adjust_time"},
						{trans_d07_data_recd_part_prog ,"trans_d07_data_recd_part_prog"},
						{trans_d07_data_recd_zone_prog ,"trans_d07_data_recd_zone_prog"},
						{trans_d07_data_recd_weekday_prog ,"trans_d07_data_recd_weekday_prog"},
						{trans_d07_data_recd_holiday_prog ,"trans_d07_data_recd_holiday_prog"},
						{trans_d07_data_recd_power_prog ,"trans_d07_data_recd_power_prog"},
						{trans_d07_data_recd_payoff_prog ,"trans_d07_data_recd_payoff_prog"},
						{trans_d07_data_recd_open_meter_cap ,"trans_d07_data_recd_open_meter_cap"},
						{trans_d07_data_recd_open_btn_box ,"trans_d07_data_recd_open_btn_box"}
						};



	
		
	int ret = 0; 
	ret = get_d07_ruler_info(inRulerID, &info);
	if(ret != E_D07_OK)	
	{
		return E_D07_ERRO_UNKOWN_ID;
	}
	else
	{
		//printf("\n---------------------[%d]---------------------\n\n", ++g_num);					
		printf(" id\t=  0x%08lX :", inRulerID);
		printf(" [%02X][%02X][%02X][%02X]\n", ucDi3, ucDi2, ucDi1, ucDi0);
	
		/* 规约信息字段：type 数据类型；payoff 结算日；rate 费率；len 数据域字节数；
		 * rdwr 读写属性；format 数据格式；func 转换函数；name 数据项名称 */
		if(E_D07_RULER_TYPE_UNKNOWN != info.type)
		{
			printf(" type\t=  %d\n", info.type);
		
			if(info.para.payoff != E_D07_PAYOFF_NULL)
			{
				printf(" payoff\t=  %d\n", info.para.payoff);
			}
			
			if(info.para.rate != E_D07_RATE_NULL)
			{
				printf(" rate\t=  %d\n", info.para.rate);
			}
			
			// data length			
			printf(" length\t=  %d\n", info.len);

			// read or write
			if(info.rdwr == E_D07_RDWR_READ_ONLY)
			{
				printf(" rdwr\t=  read only\n");
			}
			else if(info.rdwr == E_D07_RDWR_READ_WRITE)
			{
				printf(" rdwr\t=  read write\n");
			}
			else
			{
				printf(" rdwr\t=  write only\n");
			}

			//format
			printf(" format\t=  %s\n", formats[info.format]);


			printf(" func\t=  ");

			/* 只有表中登记的函数指针与该规约一致时才显示函数名，避免错位时给出误导信息 */
			if(info.func == funcs[info.format].func)
			{
				printf("%s\n", funcs[info.format].funcName);
			}

			printf(" name\t=  %s\n\n", info.name);
			
		}
	}

	return E_D07_OK;
}



/**
 * @brief  以十六进制打印一段字节
 * @param  buflen 字节数
 * @param  buf    数据缓冲区
 */
void show_packet(int buflen, char* buf)
{	
    
	int j = buflen;
	int i;
	//printf("packet length :%d\n packet content : ",j);
	for(i = 0; i < j;i++)
	{
		printf("%02X ", (unsigned char)buf[i]);
	} 
}


/* 预留接口：与真实电表联调时使用，当前为空实现 */
void test_all_ruler_d07_meter()
{



}

