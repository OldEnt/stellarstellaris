// StellarStellaris - main.c
/*
 * Much code yoinked from:
 * https://0x00sec.org/t/linux-infecting-running-processes/1097
 * https://man7.org/linux/man-pages/man2/process_vm_readv.2.html
 * https://github.com/eklitzke/ptrace-call-userspace
 * https://nullprogram.com/blog/2016/09/03/
 * https://ancat.github.io/python/2019/01/01/python-ptrace.html
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <asm/ldt.h>

#include <sys/user.h>
#include <sys/reg.h>

#include <sys/uio.h>

#include "pdlsym.h"



int main (int argc, char *argv[]){
	pid_t target;
	struct user_regs_struct regs;
	struct user_regs_struct regs_backup;
	int syscall;
	long dst;
	unsigned long addr;
	unsigned char buf[1];
	unsigned char backup_rip_code[2];
	const unsigned char replacement_rip_code[2] = {0x0f, 0x05};

	if (argc != 2){
		fprintf(stderr, "Usage:\n\t%s pid\n", argv[0]);
		exit(1);
	}

	target = atoi(argv[1]);
	printf ("+ Attempting to attach to process %d\n", target);
	if ((ptrace (PTRACE_ATTACH, target, NULL, NULL)) < 0){
		fprintf(stderr, "+ Failed to attach to process\n");
		perror("ptrace(ATTACH):");
		exit(1);
	}

	printf ("+ Waiting for process...\n");
	wait (NULL);


	printf("+ Getting process registers\n");
	if ((ptrace (PTRACE_GETREGS, target, NULL, &regs)) < 0){
		perror ("ptrace(GETREGS):");
		exit (1);
	}

	printf("+ Getting backup process registers\n");
	if ((ptrace (PTRACE_GETREGS, target, NULL, &regs_backup)) < 0){
                perror ("ptrace(GETREGS):");
                exit (1);
        }
					        
	printf("+ DEBUG: Current RIP: 0x%llx \n", regs.rip );

	char file[64];
	sprintf(file, "/proc/%ld/mem", (long)target);
	int fd = open(file, O_RDWR);

	/* Game version magic string */
	addr = 0x0000000002332FBF; 
	unsigned char version_buf[14];
	unsigned char expected_version[] = "Butler v2.8.1";
	pread(fd, &version_buf, sizeof(version_buf), addr);
	if(strcmp(&expected_version, &version_buf) != 0){
		fprintf(stderr, "\nFATAL ERROR: Invalid version string, aborting!\n");
		exit(1);
	}

	printf("+ Found Version string: %s \n", version_buf);

	regs.rax = 0x9;	// sys_mmap
	regs.rdi = 0x0;		// offset
	regs.rsi = 100*1000;	// size (100KB)
	regs.rdx = 0x7;		// map permissions
	regs.r10 = 0x22;	// anonymous
	regs.r8 = 0x0;		// fd
	regs.r9 = 0x0;		// fd

	printf("+ Setting registers for mmap, executable space\n");

	if ((ptrace (PTRACE_SETREGS, target, NULL, &regs)) < 0){
		perror ("ptrace(SETREGS):");
		exit(1);
	}

	printf("+ Backing up %lu bytes at RIP: 0x%02llx\n", sizeof(backup_rip_code), regs.rip);
	pread(fd, &backup_rip_code, sizeof(backup_rip_code), regs.rip);
	printf("+ Backed up: 0x");
	for(int i=0; i< sizeof(backup_rip_code); i++){
		printf("%02x", backup_rip_code[i]);
	}
	printf("\n");

	printf("+ Writing syscall to RIP\n");
	pwrite(fd, &replacement_rip_code, sizeof(replacement_rip_code), regs.rip);


	printf("+ Single stepping pid: %d\n", target);

	if ((ptrace (PTRACE_SINGLESTEP, target, NULL, NULL)) < 0){
		perror("ptrace(SINGLESTEP):");
		exit(1);
	}

	printf("+ Waiting for process...\n");
	wait(NULL);

	printf("+ Getting post-mmap process registers\n");
        if ((ptrace (PTRACE_GETREGS, target, NULL, &regs)) < 0){
		perror ("ptrace(GETREGS):");
		exit (1);
	}
	
	unsigned long long rwx_addr = regs.rax;

	printf("+ RWX hopefully created @%02llx\n", rwx_addr);

	printf("+ Restoring Registers from backup\n");

	if ((ptrace (PTRACE_SETREGS, target, NULL, &regs_backup)) < 0){
		perror ("ptrace(SETREGS):");
		exit(1);
        }

	printf("+ Restoring RIP code from backup\n");
	pwrite(fd, &backup_rip_code, sizeof(backup_rip_code), regs_backup.rip);


	//We are back to normal execution with our own shiny memory allocation for executable code.
	unsigned long long this_addr = rwx_addr+10000;
	const unsigned char CGuiObject_KillObject_asm[] = {
		0xc6, 0x87, 0xb0, 0x00, 0x00, 0x00, 0x01, 	// mov BYTE PTR [rdi+0xb0], 0x1
		0x53,						// push rbx
		0x48, 0xb8, 					// movabs with no address
		((this_addr) & 0xFF), 				// Our address for the list of deleting objects (actual list +0x10)
		((this_addr>>8) & 0xFF),	
		((this_addr>>16) & 0xFF),
		((this_addr>>24) & 0xFF),
		((this_addr>>32) & 0xFF),
		((this_addr>>40) & 0xFF),
		((this_addr>>48) & 0xFF),
		((this_addr>>56) & 0xFF),
		0x48, 0x8b, 0x58, 0x08,				// mov rbx, qword PTR [rax+0x8] (# of objects in list)
		0x48, 0x89, 0x3c, 0xd8,				// mov qword PTR rax+rbx*0x8], rdi
		0x48, 0xff, 0xc3,				// inc rbx
		0x48, 0x89, 0x58, 0x08,				// mov qword ptr [rax+0x8], rbx
		0x58,						// pop rax
		0x5b,						// pop rbx
		0xc3						// ret
	};

	printf("+ Writing CGuiObject::KillObject replacement, bytes: %lu to addr: 0x%02llx\n", sizeof(CGuiObject_KillObject_asm), (rwx_addr+0x100));
	pwrite(fd, &CGuiObject_KillObject_asm, sizeof(CGuiObject_KillObject_asm), rwx_addr+0x100);

	const unsigned char init_val[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 };
	//initialize to 0x02 to our first object ends up at this_addr+0x2*0x8
	pwrite(fd, &init_val, sizeof(init_val), this_addr+0x8);

/*
	addr = 0x0000000001e71b90; //_ZN18CPdxParticleObject13RenderBucketsEP9CGraphicsPK7CCamerai
	pread(fd, &buf, sizeof(buf), addr);
	printf("+ DEBUG: CPdxParticleObject::RenderBuckets addr: 0x%02hhx\n", *buf);

	//buf[0] = 0xc3; 
	buf[0] = 0x55;
	pwrite(fd, &buf, sizeof(buf), addr);


	addr = 0x00000000021229a0; //ParticleUpdate
	pread(fd, &buf, sizeof(buf), addr);
	printf("+ DEBUG: ParticleUpdate addr: 0x%02hhx\n", *buf);


	//buf[0] = 0xc3;
	buf[0] = 0x55;
	pwrite(fd, &buf, sizeof(buf), addr);




	addr = 0x00000000021db6f0; //CGui::PerFrameUpdate
	pread(fd, &buf, sizeof(buf), addr);
	printf("+ DEBUG: CGui::PerFrameUpdate addr: 0x%02hhx\n", *buf);

	//buf[0] = 0xc3;
        buf[0] = 0x41;
	pwrite(fd, &buf, sizeof(buf), addr);
	
	addr = 0x00000000021dab10; //CGui::HandelInput
	pread(fd, &buf, sizeof(buf), addr);
	printf("+ DEBUG: CGui::HandelInput addr: 0x%02hhx\n", *buf);

	//buf[0] = 0xc3;
	buf[0] = 0x55;
	pwrite(fd, &buf, sizeof(buf), addr);



	addr = 0x00000000018bc900; //COutliner::InternalUpdate
	pread(fd, &buf, sizeof(buf), addr);
	printf("+ DEBUG: COutliner::InternalUpdate addr: 0x%02hhx\n", *buf);

	//buf[0] = 0xc3;
	buf[0] = 0x55;
	pwrite(fd, &buf, sizeof(buf), addr);

*/

	close(fd);

	ptrace(PTRACE_DETACH, target, NULL, NULL);
	return 0;
}