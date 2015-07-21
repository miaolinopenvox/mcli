/*=========================================================================
mcli - interactive mlm cli tool

written by  Miao Lin <lin.miao@openvox.cn>
 
MPL
===========================================================================*/

#include <malamute.h>

#include <stdlib.h>
#include <cjson/cJSON.h>
#include <readline/readline.h>	// for readline
#include <readline/history.h>
#include <string>
#include <sstream>
#include <vector>
#include <map>	// for pair
using namespace std;

#define PRODUCT         "Mmalamute Broker Interactive CLI"
#define COPYRIGHT       "Copyright (c) 2015 "
#define HELP			"Type ? for help, exit to quit"
#define MLM_CLI_NAME	"mcli"

#define CLI_NAME_LEN 128

bool verbose = false;						// verbose print when working?
bool null_auth = false;						// not use authenticate?
const char *username = MLM_CLI_NAME;		// user/pass when login malamute server
const char *password = MLM_CLI_NAME;
const char *endpoint = "tcp://127.0.0.1:9999";

static mlm_client_t *producer = NULL;				// malamute client handler as producer
static mlm_client_t *consumer = NULL;				// malamute client handler as consumer

char producer_stream[CLI_NAME_LEN] = MLM_CLI_NAME;	// stream of producer client
char producer_name[CLI_NAME_LEN];					// name of producer client

char consumer_name[CLI_NAME_LEN];					// name of consumer client 
class string_pair_t : public pair < string, string > {};
class string_pair_list_t : public vector < string_pair_t > {};
string_pair_list_t consumer_streams;

zmq_pollitem_t stdin_pollitem;				// for stdin

const char* mlm_cli_prompt = MLM_CLI_NAME"$";	// prompt for cli
bool running = true;							// if we need exit
zloop_t* client_loop;
const char* client_ip = "127.0.0.1";			// ip address of this client

int cmd_help(char *);				// help command handler
int cmd_exit(char *);				// exit command handler
int cmd_stream(char *);		// set stream of this client
int cmd_sub(char *);			// subscribe to a stream and subject
int cmd_sendto(char *);			// set message to mailbox
int cmd_pub(char *);				// broadcast
int cmd_info(char *);				// broadcast

typedef struct {
	const char* name;	// User printable name of the function
	rl_icpfunc_t *func;	// Function to call to do the job.
	const char* doc;			// Documentation for this function.
} COMMAND;

COMMAND commands[] = {
	{ "help", cmd_help, "Print help information(this screen)" },
	{ "?", cmd_help, "Print help information(this screen)" },
	{ "exit", cmd_exit, "Exit mlm_cli"	},
	{ "quit", cmd_exit, "Exit mlm_cli"  },
	{ "setstream", cmd_stream, "Set stream for produce msg" },
	{ "sub", cmd_sub, "subscribe to stream and subject" },
	{ "sendto", cmd_sendto, "send message to client name" },
	{ "pub", cmd_pub, "broad cast message to stream" },
	{ "info", cmd_info, "show information of this client" }, 
	{ NULL, NULL, NULL	},
};

COMMAND* find_command(const char* name)
{
	int i;

	for (i = 0; commands[i].name; i++)
		if (streq(name, commands[i].name))
			return (&commands[i]);

	return ((COMMAND *)NULL);
}

// trim left and right space of string.
char * stripwhite(char *string)
{
	char *s, *t;

	for (s = string; whitespace(*s); s++)
		;

	if (*s == 0)
		return (s);

	t = s + strlen(s) - 1;
	while (t > s && whitespace(*t))
		t--;
	*++t = '\0';

	return s;
}

void SplitString(const std::string& s, std::vector<std::string>& v, const std::string& c)
{
	std::string::size_type pos1, pos2;
	pos2 = s.find(c);
	pos1 = 0;
	while (std::string::npos != pos2)
	{
		v.push_back(s.substr(pos1, pos2 - pos1));

		pos1 = pos2 + c.size();
		pos2 = s.find(c, pos1);
	}
	if (pos1 != s.length())
		v.push_back(s.substr(pos1));
}

/* Execute a command line. */
int execute_line(char *line)
{
	register int i;
	COMMAND *command;
	char *word;

	/* Isolate the command word. */
	i = 0;
	while (line[i] && whitespace(line[i]))
		i++;
	word = line + i;

	while (line[i] && !whitespace(line[i]))
		i++;

	if (line[i])
		line[i++] = '\0';

	command = find_command(word);

	if (!command)
	{
		fprintf(stderr, "%s: No such command for %s.\n", word, MLM_CLI_NAME);
		return (-1);
	}

	/* Get argument to command, if any. */
	while (whitespace(line[i]))
		i++;

	word = line + i;

	/* Call the function. */
	return ((*(command->func)) (word));
}

/* **************************************************************** */
/*                                                                  */
/*                  Interface to Readline Completion                */
/*                                                                  */
/* **************************************************************** */

char *command_generator PARAMS((const char *, int));
char **fileman_completion PARAMS((const char *, int, int));
static void cb_linehandler(char* line);

// Tell the GNU Readline library how to complete.  We want to try to complete
// on command names if this is the first word in the line 
void initialize_readline(void)
{
	// Tell the completer that we want a crack first. 
	rl_attempted_completion_function = fileman_completion;

	// Install the line handler.
	rl_callback_handler_install(mlm_cli_prompt, cb_linehandler);
}

/* Attempt to complete on the contents of TEXT.  START and END bound the
region of rl_line_buffer that contains the word to complete.  TEXT is
the word to complete.  We can use the entire contents of rl_line_buffer
in case we want to do some simple parsing.  Return the array of matches,
or NULL if there aren't any. */
char **fileman_completion(const char *text, int start, int end)
{
	char **matches;

	matches = (char **)NULL;

	/* If this word is at the start of the line, then it is a command
	to complete.  Otherwise it is the name of a file in the current
	directory. */
	if (start == 0)
		matches = rl_completion_matches(text, command_generator);

	return (matches);
}

/* Generator function for command completion.  STATE lets us know whether
to start from scratch; without any state (i.e. STATE == 0), then we
start at the top of the list. */
char *command_generator(const char *text, int state)
{
	static int list_index, len;
	const char *name;

	/* If this is a new word to complete, initialize now.  This includes
	saving the length of TEXT for efficiency, and initializing the index
	variable to 0. */
	if (!state)
	{
		list_index = 0;
		len = strlen(text);
	}

	/* Return the next name which partially matches from the command list. */
	while (name = commands[list_index].name)
	{
		list_index++;

		if (strncmp(name, text, len) == 0)
			return (strdup(name));
	}

	/* If no names matched, then return NULL. */
	return ((char *)NULL);
}

/* **************************************************************** */
/*                                                                  */
/*                       Mlm_cli Commands                           */
/*                                                                  */
/* **************************************************************** */
int cmd_help(char *arg)
{
	if (!arg)
		arg = (char*)"";

	puts("command list:");
	for (int i = 0; NULL != commands[i].name; i++) {
		printf("%s \t %s\n", commands[i].name, commands[i].doc);
	}
	return 1;
}

int cmd_exit(char *arg)
{
	puts("exiting...");

	running = false;
	return 1;
}

// set stream of this client
int cmd_stream(char *arg)
{
	if (!arg) {
		printf("\nInvalid command, use setstream stream\n");
		return 1;
	}

	arg = stripwhite(arg);
	vector<string> list;
	SplitString(string(arg), list, string(" "));
	if (0 == list.size()) {
		printf("\nInvalid command, use setstream stream\n");
		return 1;
	}

	int rc = mlm_client_set_producer(producer, list[0].c_str());
	if (rc >= 0) {
		strncpy(producer_stream, list[0].c_str(), CLI_NAME_LEN);
		producer_stream[CLI_NAME_LEN - 1] = 0;
		printf("\nstream set to %s\n", producer_stream);
	} else
		printf("\nFailed set stream to %s\n", list[0].c_str());

	return 1;
}

// subscribe to a stream and subject
int cmd_sub(char * arg)
{
	if (!arg) {
		printf("Invalid subscribe cmd, use sub stream subject\n");
		return 1;
	}
		
	arg = stripwhite(arg);
	vector<string> list;
	SplitString(string(arg), list, string(" "));

	if (list.size() != 2) {
		printf("Invalid subscribe cmd, use sub stream subject\n");
		return 1;
	}
	
	int rc = mlm_client_set_consumer(consumer, list[0].c_str(), list[1].c_str());
	if (rc >= 0) {
		printf("Subscribed to STREAM %s SUBJECT %s\n", list[0].c_str(), list[1].c_str());
		string_pair_t p;
		p.first = list[0];
		p.second = list[1];
		consumer_streams.push_back(p);
	} else
		printf("Failed subscribe to STREAM %s SUBJECT %s\n", list[0].c_str(), list[1].c_str());

	return 1;

}

// set message to mailbox
int cmd_sendto(char *arg)
{
	int rc;
	if (!arg) {
		printf("0 message send, use sendto client subject content\n");
		return 1;
	}

	arg = stripwhite(arg);
	vector<string> list;
	SplitString(string(arg), list, string(" "));
	
	if (list.size() != 3) {
		printf("0 message send, use sendto client subject content\n");
		return 1;
	} else {
		zmsg_t* msg = zmsg_new();
		zmsg_pushstr(msg, list[2].c_str());
		rc = mlm_client_sendto(	producer,				// mlm client
								list[0].c_str(),		// client address
								list[1].c_str(),		// subject
								producer_name,		// tracker ?
								1000,				// time out, 1000ms;
								&msg);				// mesage body

		if (rc >= 0)
			printf("\nSUCCESS\n");
		else
			printf("\nFAILED!\n");
	}

	return 1;
}

// broadcast
int cmd_pub(char *arg)
{
	int rc;
	if (!arg) {
		printf("0 message send, use pub subject content\n");
		return 1;
	}

	arg = stripwhite(arg);
	vector<string> list;
	SplitString(string(arg), list, string(" "));

	if (list.size() != 2 ) {
		printf("0 message send, use pub subject content\n");
		return 1;
	} else {
		printf("Broadcast to \n\tSTREAM %s\n\tSUBJECT %s\n\tCONTENT %s\n", 
				producer_stream, list[0].c_str(), list[1].c_str());
		rc = mlm_client_sendx(producer, list[0].c_str(), list[1].c_str(), NULL);
		if (rc >= 0)
			printf("SUCCESS\n");
		else
			printf("FAILED!\n");
	}

	return 1;
}

// show information
int cmd_info(char *arg)
{
	int rc;
	printf("PRODUCER NAME: %s\n"
		"PRODUCER STREAM: %s\n"
		"CONSUMER NAME : %s\n",
		producer_name, producer_stream, consumer_name);
	
	if (consumer_streams.size()) {
		string_pair_list_t::iterator it = consumer_streams.begin();

		printf("LISTENING:\n");
		while (it != consumer_streams.end()) {
			printf("STREAM: %s\nSUBJECT: %s\n", it->first.c_str(), it->second.c_str());
			it++;
		}
	} else
		printf("NOT listening to any stream\n");
	return 1;
}

// handling message for producer
static int s_mlm_producer_event(zloop_t *loop, zsock_t *handle, void *arg)
{
	//  Now receive and print any messages we get
	zmsg_t *msg = mlm_client_recv(producer);
	assert(msg);
	char *content = zmsg_popstr(msg);
	
	printf("\nMSG=>\nCommand=%s\nSubject=%s\nContent=%s\n", 
			mlm_client_command(producer), mlm_client_subject(producer), content);
	zstr_free(&content);
	zmsg_destroy(&msg);

	return 0;
}

// handling message for consumer
static int s_mlm_consumer_event(zloop_t *loop, zsock_t *handle, void *arg)
{
	//  Now receive and print any messages we get
	zmsg_t *msg = mlm_client_recv(consumer);
	assert(msg);
	char *content = zmsg_popstr(msg);

	printf("\nMSG=>\nCommand=%s\nSubject=%s\nContent=%s\n", 
			mlm_client_command(consumer), mlm_client_subject(consumer), content);
	zstr_free(&content);
	zmsg_destroy(&msg);

	return 0;
}


static void cb_linehandler(char* line)
{
	if (line == NULL ) 
		return;
		
	line = stripwhite(line);

	if (line[0]) {
		add_history(line);
		execute_line(line);
	}
	free(line);
}


// simply forward console input to readline callback.
static int s_stdin_event(zloop_t *loop, zmq_pollitem_t *item, void *arg)
{
	if (!running)
		return -1;
	else {
		rl_callback_read_char();
		if (!running)
			return -1;
		else
			return 0;
	}
}

int main(int argc, char *argv[])
{
	int argn = 1;
	int rc;

	while (argn < argc) {
		if (streq(argv[argn], "-v")) {
			verbose = true;
		} else if (streq(argv[argn], "-n")) {
			null_auth = true;
		} else if (streq(argv[argn], "-e")) {
			endpoint = argv[++argn];
		} else if (streq(argv[argn], "-a")) {
			client_ip = argv[++argn];
		} else if (streq(argv[argn], "-p")) {
			username = argv[++argn];
			password = argv[++argn];
		} else {
			printf("syntax: "MLM_CLI_NAME" [-v][-n][-e endpoint] [-a ipaddr] [-p username password]\n");
			return 0;
		}
		argn++;
	}
	
	printf(MLM_CLI_NAME": endpoint %s\n", endpoint);

	mlm_client_verbose = verbose;
	producer = mlm_client_new();
	assert(producer);
	consumer = mlm_client_new();
	assert(consumer);

	// use plain text auth to mlm server
	if (!null_auth) {
		mlm_client_set_plain_auth(producer, username, password);
		mlm_client_set_plain_auth(consumer, username, password);
	}

	sprintf(producer_name, "%s://%s/%d#producer", MLM_CLI_NAME, client_ip, getpid());
	if (mlm_client_connect(producer, endpoint, 1000, producer_name)) {
		zsys_error(MLM_CLI_NAME": server not reachable at %s", endpoint);
		mlm_client_destroy(&producer);
		mlm_client_destroy(&consumer);
		return 0;
	}
	
	sprintf(consumer_name, "%s://%s/%d#consumer", MLM_CLI_NAME, client_ip, getpid());
	if (mlm_client_connect(consumer, endpoint, 1000, consumer_name)) {
		zsys_error(MLM_CLI_NAME": server not reachable at %s", endpoint);
		mlm_client_destroy(&producer);
		mlm_client_destroy(&consumer);
		return 0;
	}

	// the client with send all message to the stream
	mlm_client_set_producer(producer, producer_stream);

	// use zloop to do the main message loop
	client_loop = zloop_new();
	assert(client_loop);

	zloop_set_verbose(client_loop, verbose);

	// add producer
	rc = zloop_reader(client_loop, mlm_client_msgpipe(producer), s_mlm_producer_event, NULL);
	assert(rc == 0);

	// add consumer
	rc = zloop_reader(client_loop, mlm_client_msgpipe(consumer), s_mlm_consumer_event, NULL);
	assert(rc == 0);

	// add dahdi udp socket to zloop reactor through raw socket method 
	stdin_pollitem.socket = NULL;
	stdin_pollitem.fd = STDIN_FILENO;	/* for stdin */
	stdin_pollitem.events = ZMQ_POLLIN;

	rc = zloop_poller(client_loop, &stdin_pollitem, s_stdin_event, NULL);
	assert(rc == 0);

	puts(PRODUCT);
	puts(COPYRIGHT);
	puts(HELP);
	initialize_readline();

	// start message loop forever.
	zloop_start(client_loop);

	rl_callback_handler_remove();
	zloop_poller_end(client_loop, &stdin_pollitem);
	zloop_reader_end(client_loop, mlm_client_msgpipe(producer));
	zloop_reader_end(client_loop, mlm_client_msgpipe(consumer));

	printf(MLM_CLI_NAME": zloop stopped\n");
	zloop_destroy(&client_loop);
	mlm_client_destroy(&producer);
	mlm_client_destroy(&consumer);

	return 0;
}
