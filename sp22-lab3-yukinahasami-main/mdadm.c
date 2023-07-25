#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"

//variable for later use in checking in mount and unmount
//if mounted then 1 if unmounted then 0
int checkstatus=0;

//helper: this is a op system
uint32_t wholethingop(jbod_cmd_t comand, uint32_t disk, uint32_t reserve, uint32_t block){
   //set up a op system contain only 0's since in this case, everything except command field can be ingrone.
   //I let a variable named comand and assign JBOD_MOUNT, or UNMONUT or any other comand in to it as the command field
   //use bit opreration to shift and combine the op and the command, blockid reserve and diskid fields 
  uint32_t wholething = 0;
  comand = comand << 26;
  disk = disk << 22;
  reserve = reserve <<8;
  block = block;
  wholething=wholething|comand|disk|reserve|block;
  return wholething;
}

int mdadm_mount(void) {

  //first check whether it is already mounted
  if (checkstatus == 1)
  {
    //yes, then fail
    return -1;
  }else{
    //otherwise, mount it.
      uint32_t Imop=wholethingop(JBOD_MOUNT,0,0,0);
      //according to instruction I assign pointer be NULL
      uint8_t *Impointer = NULL;

      //call opreation to see whether it is accpeting and base on the return value we determine whether or not it is mounted or not.
      int check = jbod_operation(Imop,Impointer);

     //if success
      if (check == 0){
        //return 1 for success
        //and update status to be mounted.
        checkstatus =1;
        return 1;
      }else{
        //otherwise return for failure
        return -1;
      }
      
  }
  
     
}

int mdadm_unmount(void) {

//same as mount this to check whether or not it has been mounted before it unmounted again.
  if (checkstatus == 0)
    {
      return -1;
    }
    else{
      // //set up variable as TA and instruction suggested.
      uint32_t Iamaop=wholethingop(JBOD_UNMOUNT,0,0,0);
      uint8_t *Impointer = NULL;
      int check = jbod_operation(Iamaop,Impointer);
        if (check == 0){
          //return 1 for success
          //update status to unmount.
          checkstatus=0;
          return 1;
        }else{
          //otherwise return for failure
          return -1;
        }
        
    }
    
     
}


int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  //calculation for diskid, blockid and which bit we are current at, I call it remainbit.
  uint32_t diskid = addr/(65536);
  uint32_t blockid = (addr%65536)/256;
  uint32_t remainbit = (addr%65536)%256;
  
  //read will only happen when it is mount
  if (checkstatus==1)
  {
      //special suitation that can also be accepted.
    if((addr == 0)&&(len == 0)&&(buf == NULL)){
      return 0;
    }
    //can not exceed the len of 1024 of not equal size.
    if(((addr+len)>JBOD_NUM_DISKS*JBOD_DISK_SIZE)||(len > 1024)){
      return -1;
    }
    if((buf==NULL)&&(len!=0)){
      return -1;
    }else{//if there is no invaild paramters.
      //seek the disk and the block
      uint32_t gotodisk=wholethingop(JBOD_SEEK_TO_DISK,diskid,0,0);
      uint32_t gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
      //printf("disk:%d, block:%d, offset:%d ",diskid,blockid,remainbit);
      //call operation
      jbod_operation(gotodisk, NULL);
      jbod_operation(gotoblock,NULL);

      //create a temparay buf
      void *tem=malloc(JBOD_BLOCK_SIZE);
      //read block with calculaterd disk and block id
      uint32_t readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
      jbod_operation(readcomand,tem);

      //check condition to determine what suitation is reading
      if (len<=(256-remainbit))
      {//reading within a block
        //in this case we can just directly copy it to the buf
        memcpy(buf,tem+remainbit,len);
        
      }else if((len>(256-remainbit))&&(len<256)){
      

          if(blockid==255){
            //reading across the disk with the len given is less than 256
            //since cross, set up at least 2 to check block 1 and next block's length
            int lastdiskread = 256-remainbit;
            int unread = len - lastdiskread;
            //copy it into buf with the first block from the last disk
            memcpy(buf,tem+remainbit,lastdiskread);
            //update to the next disk
            diskid += 1;
            gotodisk=wholethingop(JBOD_SEEK_TO_DISK,diskid,0,blockid);

            //update disk to next and block will get back to 0
            blockid = 0;
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);

            //repeat call jbod operation
            jbod_operation(gotodisk, NULL);
            jbod_operation(gotoblock,NULL);
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);
            
            //copy the memory buf with the second block from the second disk.
            memcpy(buf+(lastdiskread),tem,unread);
          }
          //spc6092@psu.edu
          else{     
            //reading across the block
            //since cross 1 block, set up at 2 variable to check block 1 and next block's length
            int readpart = 256-remainbit;
            int unread = len - readpart;
            //copy the first one
            memcpy(buf,tem+remainbit,readpart);

            //update to next block
            blockid +=1;
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);
            //printf("read: %d, unread: %d",readpart,unread);

            //copy the memory buf with the second block.
            memcpy(buf+(readpart),tem,unread);
          }
     

      }else if (len>=256)
      {//when reads three block it should be more than 256
    
        if(blockid==255){//since read cross disk can also happened with 3 blocks
            //reading across the disk, looks exactly the same as the one from last.
            int lastdiskread = 256-remainbit;
            int unread = len - lastdiskread;
            memcpy(buf,tem+remainbit,lastdiskread);

            diskid += 1;
            gotodisk=wholethingop(JBOD_SEEK_TO_DISK,diskid,0,blockid);
            blockid = 0;

            
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotodisk, NULL);
            jbod_operation(gotoblock,NULL);
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);
        
            memcpy(buf+(lastdiskread),tem,unread);
          }

          else{
            //read across three blocks
            //since we have three, we have three variable to store 3 lengths
            int read1=256-remainbit;
            int read2=256;
            int read3=len-read1-read2;
            //always copy the first one at the beginning
            memcpy(buf,tem+remainbit,read1);

            //update to the second block
            blockid+=1;       
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);
            //copy
            memcpy(buf+read1,tem,read2);

            //update to the third block
            blockid+=1;
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);
            //copy
            memcpy(buf+(read1+read2),tem,read3);

      
          }  
      


      }
        //free up space
        free(tem);
        
        return len;
      
    }
  }
  else{
    //if it is not mounted, just directly fail.
    return -1;
  }

}


int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
  //calculation for diskid, blockid and which bit we are current at, I call it remainbit.
  uint32_t diskid = addr/(65536);
  uint32_t blockid = (addr%65536)/256;
  uint32_t remainbit = (addr%65536)%256;
  
  //write and read will only happen when it is mount
  if (checkstatus==1)
  {
      //special suitation that can also be accepted.
    if((addr == 0)&&(len == 0)&&(buf == NULL)){
      return 0;
    }
    //can not exceed the len of 1024 of not equal size.
    if(((addr+len)>JBOD_NUM_DISKS*JBOD_DISK_SIZE)||(len > 1024)){
      return -1;
    }
    if((buf==NULL)&&(len!=0)){
      return -1;
    }else{//if there is no invaild paramters.
      //seek the disk and the block
      uint32_t gotodisk=wholethingop(JBOD_SEEK_TO_DISK,diskid,0,0);
      uint32_t gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
      //printf("disk:%d, block:%d, offset:%d ",diskid,blockid,remainbit);
      //call operation
      jbod_operation(gotodisk, NULL);
      jbod_operation(gotoblock,NULL);

      //create a temparay buf and a buf to store the constant buf
      void *tem=malloc(JBOD_BLOCK_SIZE);
      void *Iambuf = malloc(len);
      memcpy(Iambuf,buf,len);
      
      //write block with calculaterd disk and block id
      //uint32_t readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
      uint32_t writecomand=wholethingop(JBOD_WRITE_BLOCK,diskid,0,blockid);
      uint32_t readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
      //check condition to determine what suitation is writing
      if (len<=(256-remainbit))
      {//writing within a block
        //in this case we can just directly copy it to the buf
        //printf("len: %d", len)
        readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
        jbod_operation(readcomand,tem);

        memcpy(tem+remainbit,Iambuf,len);
        gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
        jbod_operation(gotoblock,NULL);
        jbod_operation(writecomand,tem);
        
      }else if((len>(256-remainbit))&&(len<256)){
      

          if(blockid==255){
            //writing across the disk with the len given is less than 256
            //since cross, set up at least 2 to check block 1 and next block's length
            int lastdiskwr = 256-remainbit;
            int unwrite = len - lastdiskwr;
            //printf("len: %d",len);
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);

        
            //copy it into buf with the first block from the last disk
            memcpy(tem+remainbit,Iambuf,lastdiskwr);
            gotodisk=wholethingop(JBOD_SEEK_TO_DISK,diskid,0,blockid);
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotodisk,NULL);
            jbod_operation(gotoblock,NULL);
            jbod_operation(writecomand,tem);

            //update to the next disk
            diskid += 1;
            gotodisk=wholethingop(JBOD_SEEK_TO_DISK,diskid,0,blockid);

            //update disk to next and block will get back to 0
            blockid = 0;
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);

            //repeat call jbod operation
            jbod_operation(gotodisk, NULL);
            jbod_operation(gotoblock,NULL);
            
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);
            //copy the memory buf with the second block from the second disk.
            memcpy(tem+(lastdiskwr),Iambuf+unwrite,unwrite);
            gotodisk=wholethingop(JBOD_SEEK_TO_DISK,diskid,0,blockid);
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotodisk,NULL);
            jbod_operation(gotoblock,NULL);
            jbod_operation(writecomand,tem+unwrite);
          }
          else{     
            //wrtiting across the block
            //since cross 1 block, set up at 2 variable to check block 1 and next block's length
            int readpart = 256-remainbit;
            int unread = len - readpart;
            //printf("len: %d", len);
            //copy the first one
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);
           
            memcpy(tem+remainbit,Iambuf,readpart);
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);
            jbod_operation(writecomand,tem);

            //update to next block
            blockid +=1;
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);
  
            writecomand=wholethingop(JBOD_WRITE_BLOCK,diskid,0,blockid);
            
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);
            //printf("read: %d, unread: %d",readpart,unread);


            //copy the memory buf with the second block.
            memcpy(tem+(readpart),Iambuf+unread,unread);
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);
            jbod_operation(writecomand,tem+unread);
          }
     

      }else if (len>=256)
      {//when reads three block it should be more than 256
    
        if(blockid==255){//since write cross disk can also happened with 3 blocks
            //writing across the disk, looks exactly the same as the one from last.
            int lastdiskwr = 256-remainbit;
            int unwrite = len - lastdiskwr;
            //printf("len: %d",len);
            //copy it into buf with the first block from the last disk
            memcpy(tem+remainbit,Iambuf,lastdiskwr);
            jbod_operation(writecomand,tem);

            //update to the next disk
            diskid += 1;
            gotodisk=wholethingop(JBOD_SEEK_TO_DISK,diskid,0,blockid);

            //update disk to next and block will get back to 0
            blockid = 0;
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);

            //repeat call jbod operation
            jbod_operation(gotodisk, NULL);
            jbod_operation(gotoblock,NULL);
            
  
            //copy the memory buf with the second block from the second disk.
            memcpy(tem+(lastdiskwr),Iambuf+unwrite,unwrite);
            jbod_operation(writecomand,tem+unwrite);
          }
      

          else{
            //write across three blocks
            //since we have three, we have three variable to store 3 lengths
            int read1=256-remainbit;
            int read2=256;
            int read3=len-read1-read2;
            //printf("read1: %d", remainbit);
            //always copy the first one at the beginning
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);

            memcpy(tem+remainbit,Iambuf,read1);
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);
            jbod_operation(writecomand,tem);



            ////update to the second block
            blockid +=1;
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);
  
            writecomand=wholethingop(JBOD_WRITE_BLOCK,diskid,0,blockid);
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);
            //copy=
            memcpy(tem+read1,Iambuf+read1, read2);
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);
            jbod_operation(writecomand,tem+read1);


            ////update to the third block
            blockid+=1;
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);

            writecomand=wholethingop(JBOD_WRITE_BLOCK,diskid,0,blockid);
            readcomand=wholethingop(JBOD_READ_BLOCK,diskid,0,blockid);
            jbod_operation(readcomand,tem);
            //copy
            memcpy(tem+(read1+read2),Iambuf+read1+read2,read3);
            gotoblock=wholethingop(JBOD_SEEK_TO_BLOCK,diskid,0,blockid);
            jbod_operation(gotoblock,NULL);
            jbod_operation(writecomand,tem+read1+read2);
      
          }  
      



  }
        //free up space
        free(tem);
        free(Iambuf);
        return len;
      
    }
  }
  else{
    //if it is not mounted, just directly fail.
    return -1;
  }

}
