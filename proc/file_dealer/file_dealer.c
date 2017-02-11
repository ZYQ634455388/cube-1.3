#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "data_type.h"
#include "alloc.h"
#include "string.h"
#include "basefunc.h"
#include "struct_deal.h"
#include "crypto_func.h"
#include "memdb.h"
#include "message.h"
#include "ex_module.h"

#include "file_dealer.h"
#include "file_struct.h"

int file_dealer_init(void * sub_proc,void * para)
{
	int ret=0;
	return ret;
}

int file_dealer_start(void * sub_proc,void * para)
{
	int ret;
	void * recv_msg;
	void * context;
	int i;
	int type;
	int subtype;

	printf("begin file_dealer start process! \n");

	for(i=0;i<3000*1000;i++)
	{
		usleep(time_val.tv_usec);
		ret=ex_module_recvmsg(sub_proc,&recv_msg);
		if(ret<0)
			continue;
		if(recv_msg==NULL)
			continue;

 		type=message_get_type(recv_msg);
 		subtype=message_get_subtype(recv_msg);
		if(type==NULL)
		{
			message_free(recv_msg);
			continue;
		}
		if(!memdb_find_recordtype(type,subtype))
		{
			printf("message format (%d %d) is not registered!\n",
				message_get_type(recv_msg),message_get_subtype(recv_msg));
			continue;
		}
		if(type==DTYPE_FILE_TRANS)
		{
			if(subtype==SUBTYPE_FILE_DATA)
			{
				proc_file_receive(sub_proc,recv_msg);
			}
			else if(subtype==SUBTYPE_FILE_QUEST)
			{
				proc_file_send(sub_proc,recv_msg);
			}
		}
	}
	return 0;
};


int _is_samefile_exists(void * record)
{
	struct policyfile_data * pfdata=record;
	char digest[DIGEST_SIZE];
        char uuid[DIGEST_SIZE*2];
	int fd;
	int ret;
	fd=open(pfdata->filename,O_RDONLY);
	if(fd>0)
	{
		close(fd);
		ret=calculate_sm3(pfdata->filename,digest);
		if(ret<0)
			return ret;
		digest_to_uuid(digest,uuid);
		if(strncmp(pfdata->uuid,uuid,DIGEST_SIZE*2)!=0)
		{
			return 1;
		}
		return 0;
	}
	return 2;	

}


int proc_file_receive(void * sub_proc,void * message)
{
	struct policyfile_data * pfdata;
	struct policyfile_store * storedata;
	int ret;

	printf("begin file receive!\n");
	char buffer[1024];
	char digest[DIGEST_SIZE];
        char uuid[DIGEST_SIZE*2];
	int blobsize=0;
	int fd;

	ret=message_get_record(message,&pfdata,0);
	if(ret<0)
		return -EINVAL;
	if(pfdata->total_size==pfdata->data_size)
	{
		// judge if there exists a same-name file 
		switch(ret=_is_samefile_exists(pfdata))
		{
			case 0:     // samefile exists
				printf("file %s has existed!\n",pfdata->filename);
				return 0;
			case 1:
				printf("overwrite the file %s!\n",pfdata->filename);
				ret=remove(pfdata->filename);
				if(ret<0)
					return ret;
			case 2:
			{
				ret=get_filedata_from_message(message);
				struct policyfile_notice * pfnotice;
				pfnotice=malloc(sizeof(struct policyfile_notice));
				if(pfnotice==NULL)
					return -ENOMEM;
				memcpy(pfnotice->uuid,pfdata->uuid,DIGEST_SIZE*2);
				pfnotice->filename=dup_str(pfdata->filename,0);
				if(_is_samefile_exists(pfdata)==0)
				{
					pfnotice->result=1;	
				}
				else
				{
					pfnotice->result=0;	
				}
				void * send_msg=message_create(DTYPE_FILE_TRANS,SUBTYPE_FILE_NOTICE,message);
				if(send_msg==NULL)
					return -EINVAL;
				message_add_record(send_msg,pfnotice);
				ex_module_sendmsg(sub_proc,send_msg);
				return ret;
			}
			default:
				return ret;
		}
	}
	else
	{
		ret=FindPolicy(pfdata->uuid,"FILS",&storedata);
		if(ret<0)
			return ret;
		if(storedata==NULL)
		{
			switch(ret=_is_samefile_exists(pfdata))
			{
				case 0:     // samefile exists
					printf("file %s has existed!\n",pfdata->filename);
					return 0;
				case 1:
					printf("overwrite the file %s!\n",pfdata->filename);
					ret=remove(pfdata->filename);
					if(ret<0)
						return ret;
					break;
				case 2:
					break;

				default:
					return ret;
			}
			storedata=malloc(sizeof(struct policyfile_store));
			if(storedata==NULL)
				return -ENOMEM;
			memcpy(storedata->uuid,pfdata->uuid,DIGEST_SIZE*2);				
			storedata->filename=dup_str(pfdata->filename,0);
			storedata->file_size=pfdata->total_size;
			storedata->block_size=256;
			storedata->block_num=(pfdata->total_size+(256-1))/256;
			storedata->mark_len=(storedata->block_num+7)/8;
			storedata->marks=malloc(storedata->mark_len);
			if(storedata->marks==NULL)
				return -ENOMEM;
			memset(storedata->marks,0,storedata->mark_len);
		}


		int site= pfdata->offset/256;
		bitmap_set(storedata->marks,site);
		AddPolicy(storedata,"FILS");	
		ret=get_filedata_from_message(message);
		if(ret<0)
			return ret;
	
		if(bitmap_is_allset(storedata->marks,storedata->block_num))
		{
			printf("get file %s succeed!\n",pfdata->filename);
			struct policyfile_notice * pfnotice;
			pfnotice=malloc(sizeof(struct policyfile_notice));
			if(pfnotice==NULL)
				return -ENOMEM;
			memcpy(pfnotice->uuid,pfdata->uuid,DIGEST_SIZE*2);
			pfnotice->filename=dup_str(pfdata->filename,0);
			if(_is_samefile_exists(pfdata)==0)
			{
				pfnotice->result=1;	
			}
			else
			{
				pfnotice->result=0;	
			}
			void * send_msg=message_create(DTYPE_FILE_TRANS,SUBTYPE_FILE_NOTICE,message);
			if(send_msg==NULL)
				return -EINVAL;
			message_add_record(send_msg,pfnotice);
			ex_module_sendmsg(sub_proc,send_msg);
			return pfdata->data_size;
		}
	}

	return 0;
}
int proc_file_send(void * sub_proc,void * message)
{

	struct policyfile_data * pfdata;
	struct policyfile_req  * reqdata;
	int ret;
	int fd;
	int block_size=256;
	int data_size;
	int total_size;
	char digest[DIGEST_SIZE];
	char uuid[DIGEST_SIZE*2];
        struct stat statbuf;
	void * send_msg;
	printf("begin file send!\n");

	ret=message_get_record(message,&reqdata,0);
	if(ret<0)
		return -EINVAL;
	
	if(reqdata->filename==NULL)
		return -EINVAL;
	fd=open(reqdata->filename,O_RDONLY);
	if(fd<0)
		return fd;
        if(fstat(fd,&statbuf)<0)
        {
                printf("fstat error\n");
                return NULL;
        }
        total_size=statbuf.st_size;
	close(fd);

	calculate_sm3(reqdata->filename,digest);
	digest_to_uuid(digest,uuid);

	int i;
		
	fd=open(reqdata->filename,O_RDONLY);
	if(fd<0)
		return fd;
		
	for(i=0;i<total_size/block_size;i++)
	{
		
        	pfdata=malloc(sizeof(struct policyfile_data));
		if(pfdata==NULL)
		{
			return NULL;
		}
        	memset(pfdata,0,sizeof(struct policyfile_data));
        	strncpy(pfdata->uuid,uuid,64);
        	pfdata->filename=dup_str(reqdata->filename,0);
        	pfdata->record_no=i;
        	pfdata->offset=i*block_size;
        	pfdata->total_size=total_size;
        	pfdata->data_size=block_size;
		pfdata->policy_data=(char *)malloc(sizeof(char)*data_size);

        	if(read(fd,pfdata->policy_data,block_size)!=block_size)
        	{
                	printf("read vm list error! \n");
                	return NULL;
        	}
		send_msg=message_create(DTYPE_FILE_TRANS,SUBTYPE_FILE_DATA,message);
		if(send_msg==NULL)
			return -EINVAL;
		message_add_record(send_msg,pfdata);
		ex_module_sendmsg(sub_proc,send_msg);
			
	}

	if((data_size=total_size%block_size)!=0)
	{
        	pfdata=malloc(sizeof(struct policyfile_data));
		if(pfdata==NULL)
		{
			return NULL;
		}
        	memset(pfdata,0,sizeof(struct policyfile_data));
        	strncpy(pfdata->uuid,uuid,64);
        	pfdata->filename=dup_str(reqdata->filename,0);
        	pfdata->record_no=i;
        	pfdata->offset=i*block_size;
        	pfdata->total_size=total_size;
        	pfdata->data_size=data_size;
		pfdata->policy_data=(char *)malloc(sizeof(char)*data_size);

        	if(read(fd,pfdata->policy_data,data_size)!=data_size)
        	{
                	printf("read vm list error! \n");
                	return NULL;
        	}
		send_msg=message_create(DTYPE_FILE_TRANS,SUBTYPE_FILE_DATA,message);
		if(send_msg==NULL)
			return -EINVAL;
		message_add_record(send_msg,pfdata);
		ex_module_sendmsg(sub_proc,send_msg);
	}
	close(fd);
	printf("send file %s succeed!\n",reqdata->filename);
	return i;
}