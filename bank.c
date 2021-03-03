#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "options.h"

#define MAX_AMOUNT 20

struct bank {
	int num_accounts;        // number of accounts
	int *accounts;           // balance array
	pthread_mutex_t **mutex; // mutex array
	pthread_cond_t **cond;   // cond array
	int wait;                // wait for all deposits and transfers
};

struct args {
	int		thread_num;       // application defined thread #
	int		delay;			  // delay between operations
	int		iterations;       // number of operations
	int     net_total;        // total amount deposited by this thread
	struct bank *bank;        // pointer to the bank (shared with other threads)
};

struct thread_info {
	pthread_t    id;        // id returned by pthread_create()
	struct args *args;      // pointer to the arguments
};

// Threads run on this function
void *deposit(void *ptr)
{
	struct args *args =  ptr;
	int amount, account, balance;

	while(args->iterations--) {
		amount  = rand() % MAX_AMOUNT;
		account = rand() % args->bank->num_accounts;

		pthread_mutex_lock(args->bank->mutex[account]);

		printf("Thread %d depositing %d on account %d\n",
			args->thread_num, amount, account);

		balance = args->bank->accounts[account];
		if(args->delay) usleep(args->delay); // Force a context switch

		balance += amount;
		if(args->delay) usleep(args->delay);

		args->bank->accounts[account] = balance;
		if(args->delay) usleep(args->delay);

		args->net_total += amount;

		pthread_mutex_unlock(args->bank->mutex[account]);
		pthread_cond_broadcast(args->bank->cond[account]);
	}
	return NULL;
}

void *transfer(void *ptr)
{
	struct args *args =  ptr;
	int amount, account1, account2, balance1, balance2;

	while(args->iterations--) {
		account1 = rand() % args->bank->num_accounts;
		do
			account2 = rand() % args->bank->num_accounts;
		while (account1 == account2);

		while(1) {

			pthread_mutex_lock(args->bank->mutex[account1]);
			if (pthread_mutex_trylock(args->bank->mutex[account2])) {
				pthread_mutex_unlock(args->bank->mutex[account1]);
				continue;
			}

			balance1 = args->bank->accounts[account1];
			if(args->delay) usleep(args->delay);

			balance2 = args->bank->accounts[account2];
			if(args->delay) usleep(args->delay);

			amount = (balance1 == 0) ? 0 : rand() % (balance1 + 1);

			printf("Thread %d transferring %d from account %d to account %d\n",
				args->thread_num, amount, account1, account2);

			balance1 -= amount;
			if(args->delay) usleep(args->delay);

			balance2 += amount;
			if(args->delay) usleep(args->delay);

			args->bank->accounts[account1] = balance1;
			if(args->delay) usleep(args->delay);

			args->bank->accounts[account2] = balance2;
			if(args->delay) usleep(args->delay);

			pthread_mutex_unlock(args->bank->mutex[account1]);
			pthread_mutex_unlock(args->bank->mutex[account2]);
			pthread_cond_broadcast(args->bank->cond[account2]);
			break;
		}
	}
	return NULL;
}

void *withdraw(void *ptr)
{
	struct args *args =  ptr;
	int amount, account, balance;

	while(args->iterations--) {
		amount  = rand() % MAX_AMOUNT;
		account = rand() % args->bank->num_accounts;

		pthread_mutex_lock(args->bank->mutex[account]);

		while ((amount > args->bank->accounts[account]) && (args->bank->wait))
			pthread_cond_wait(args->bank->cond[account], args->bank->mutex[account]);

		if (amount <= args->bank->accounts[account])
		{
			printf("Thread %d withdrawing %d from account %d\n",
				args->thread_num, amount, account);

			balance = args->bank->accounts[account];
			if(args->delay) usleep(args->delay);

			balance -= amount;
			if(args->delay) usleep(args->delay);

			args->bank->accounts[account] = balance;
			if(args->delay) usleep(args->delay);

			args->net_total += amount;
		} else {
			printf("Thread %d could not withdraw %d from account %d\n",
				args->thread_num, amount, account);
		}

		pthread_cond_broadcast(args->bank->cond[account]);
		pthread_mutex_unlock(args->bank->mutex[account]);
	}
	return NULL;
}

// start opt.num_threads threads running on deposit.
struct thread_info *start_threads(struct options opt, struct bank *bank)
{
	int i;
	struct thread_info *threads;
	int total_threads = opt.num_threads * 3;

	printf("creating %d threads\n", total_threads);
	threads = malloc(sizeof(struct thread_info) * total_threads);

	if (threads == NULL) {
		printf("Not enough memory\n");
		exit(1);
	}

	// Create num_thread threads running swap()
	for (i = 0; i < total_threads; i++) {
		threads[i].args = malloc(sizeof(struct args));

		threads[i].args -> thread_num = i;
		threads[i].args -> net_total  = 0;
		threads[i].args -> bank       = bank;
		threads[i].args -> delay      = opt.delay;
		threads[i].args -> iterations = opt.iterations;

		if (i < opt.num_threads) {
			if (0 != pthread_create(&threads[i].id, NULL, deposit, threads[i].args)) {
				printf("Could not create thread #%d", i);
				exit(1);
			}
		} else if (i < opt.num_threads * 2) {
			if (0 != pthread_create(&threads[i].id, NULL, transfer, threads[i].args)) {
				printf("Could not create thread #%d", i);
				exit(1);
			}
		} else {
			if (0 != pthread_create(&threads[i].id, NULL, withdraw, threads[i].args)) {
				printf("Could not create thread #%d", i);
				exit(1);
			}
		}
	}

	return threads;
}

// Print the final balances of accounts and threads
void print_balances(struct bank *bank, struct thread_info *thrs, int num_threads) {
	int total_deposits=0, total_withdrawals=0, bank_total=0;
	printf("\nNet deposits by thread\n");

	for(int i=0; i < num_threads; i++) {
		printf("%d: %d\n", i, thrs[i].args->net_total);
		total_deposits += thrs[i].args->net_total;
	}
	printf("Total: %d\n", total_deposits);

	printf("\nNet withdrawals by thread\n");

	for(int i=num_threads * 2; i < num_threads * 3; i++) {
		printf("%d: %d\n", i, thrs[i].args->net_total);
		total_withdrawals += thrs[i].args->net_total;
	}
	printf("Total: %d\n", total_withdrawals);

    printf("\nAccount balance\n");
	for(int i=0; i < bank->num_accounts; i++) {
		printf("%d: %d\n", i, bank->accounts[i]);
		bank_total += bank->accounts[i];
	}
	printf("Total: %d\n", bank_total);

}

// wait for all threads to finish, print totals, and free memory
void wait(struct options opt, struct bank *bank, struct thread_info *threads) {
	// Wait for the threads to finish
	for (int i = 0; i < opt.num_threads * 2; i++)
		pthread_join(threads[i].id, NULL);

	bank->wait = 0;

	for (int i = 0; i < opt.num_accounts; i++)
		pthread_cond_broadcast(bank->cond[i]);

	for (int i = opt.num_threads * 2; i < opt.num_threads * 3; i++)
		pthread_join(threads[i].id, NULL);

	print_balances(bank, threads, opt.num_threads);

	for (int i = 0; i < opt.num_threads * 3; i++)
		free(threads[i].args);

	for (int i = 0; i < opt.num_accounts; i++) {
		pthread_mutex_destroy(bank->mutex[i]);
		free(bank->mutex[i]);
		pthread_cond_destroy(bank->cond[i]);
		free(bank->cond[i]);
	}

	free(bank->mutex);
	free(bank->cond);
	free(threads);
	free(bank->accounts);
}

// allocate memory, and set all accounts to 0
void init_accounts(struct bank *bank, int num_accounts) {

	pthread_mutex_t **mutex;
	pthread_cond_t **cond;

	bank->num_accounts = num_accounts;
	bank->accounts     = malloc(bank->num_accounts * sizeof(int));
	mutex = malloc(sizeof(pthread_mutex_t) * (bank->num_accounts));
	cond = malloc(sizeof(pthread_cond_t) * (bank->num_accounts));

	if (mutex == NULL) {
		printf("Not enough memory\n");
		exit(1);
	}

	if (cond == NULL) {
		printf("Not enough memory\n");
		exit(1);
	}

	for(int i=0; i < bank->num_accounts; i++) {
		bank->accounts[i] = 0;
		mutex[i] = malloc(sizeof(pthread_mutex_t));

		if (mutex[i] == NULL) {
			printf("Not enough memory\n");
			exit(1);
		}

		cond[i] = malloc(sizeof(pthread_cond_t));

		if (cond[i] == NULL) {
			printf("Not enough memory\n");
			exit(1);
		}

		pthread_mutex_init(mutex[i], NULL);
		pthread_cond_init(cond[i], NULL);
	}

	bank->mutex = mutex;
	bank->cond = cond;
	bank->wait = 1;
}

int main (int argc, char **argv)
{
	struct options      opt;
	struct bank         bank;
	struct thread_info *thrs;

	srand(time(NULL));

	// Default values for the options
	opt.num_threads  = 5;
	opt.num_accounts = 10;
	opt.iterations   = 100;
	opt.delay        = 10;

	read_options(argc, argv, &opt);

	init_accounts(&bank, opt.num_accounts);

	thrs = start_threads(opt, &bank);
    wait(opt, &bank, thrs);

	return 0;
}
