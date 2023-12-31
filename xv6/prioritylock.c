#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "prioritylock.h"

void initprioritylock(struct prioritylock *plk, char *name)
{
  initlock(&plk->slk, "spin lock");
  plk->name = name;
  plk->pid_locked = 0;
  plk->head = 0;
}

void add_to_queue(struct prioritylock *plk)
{
  struct node* temp = (struct node *)kalloc();
  temp->priority = myproc()->pid;
  temp->next = 0;
  if(plk->head == 0)
    plk->head = temp;
  else
  {
    struct node* trace = plk->head;
    while(trace != 0)
    {
      if(trace->next->priority > temp->priority)
      {
    cprintf("mmmmmmmm\n");
        temp->next = trace->next;
        trace->next = temp;
        break;
      } 
      trace = trace->next;
    }
  }
}

void print_queue(struct prioritylock *plk)
{
  struct node *temp = plk->head;
  int i = 0;
  while (temp != 0)
  {
    i++;
    cprintf("%d ", temp->priority);
    temp = temp->next;
  }
  cprintf("ttt%dttt",i);
  cprintf("\n");
}

void remove_from_queue(struct prioritylock *plk)
{
  struct node *temp = plk->head;
  struct node *prev = temp;
  if (temp != 0 && temp->priority == plk->pid_locked)
  {
    plk->head = temp->next;
    // kfree((char *)temp);
  }
  else
  {
    while (temp != 0 && temp->priority != plk->pid_locked)
    {
      prev = temp;
      temp = temp->next;
    }
    prev->next = temp->next;
    // kfree((char *)temp);
  }
}

void acquirepriority(struct prioritylock *plk)
{
  acquire(&plk->slk);
  add_to_queue(plk);
  while (plk->pid_locked != myproc()->pid && plk->pid_locked != 0)
  {
    sleep(plk, &plk->slk);
  }
  print_queue(plk);
  plk->pid_locked = myproc()->pid;
  remove_from_queue(plk);
  release(&plk->slk);
}

void releasepriority(struct prioritylock *plk)
{
  if(myproc()->pid == plk->pid_locked)
  {
    acquire(&plk->slk);
    if (plk->head == 0 || plk->head->priority == 0)
      plk->pid_locked = 0;
    else
      plk->pid_locked = plk->head->priority;
    wakeup(plk);
    release(&plk->slk);
  }
}