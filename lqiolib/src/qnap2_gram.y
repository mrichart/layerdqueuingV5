/*
 * $id: srvn_gram.y 14381 2021-01-19 18:52:02Z greg $
 */

%{
#include <stdio.h>
#include <string.h>
#include "qnap2_document.h"

extern void qnap2error( const char * fmt, ... );
extern int qnap2lex();
%}

%union {
    int aCode;
    long aLong;
    double aReal;
    char * aString;
    void * aPointer;
}

%token QNAP_CONTROL QNAP_DECLARE QNAP_END_PROGRAM QNAP_EXEC QNAP_RESTART QNAP_REBOOT QNAP_STATION QNAP_TERMINAL
%token QNAP_INIT QNAP_NAME QNAP_PRIO QNAP_QUANTUM QNAP_RATE QNAP_SCHED QNAP_SERVICE QNAP_TRANSIT QNAP_TYPE	/* /station/ */
%token QNAP_ENTRY QNAP_EXIT

%token QNAP_ASSIGNMENT
%token QNAP_ALL QNAP_ANY QNAP_BEGIN QNAP_DO QNAP_ELSE QNAP_END QNAP_FALSE QNAP_FOR QNAP_FORWARD QNAP_GENERIC
%token QNAP_GOTO QNAP_IF QNAP_IN  QNAP_IS QNAP_NIL QNAP_OBJECT
%token QNAP_REF QNAP_REPEAT QNAP_STEP QNAP_THEN QNAP_TRUE QNAP_UNTIL QNAP_VAR QNAP_WATCHED QNAP_OPTION
%token QNAP_WHILE QNAP_WITH
%token RANGE_ERR

%token <aString>	STRING QNAP_IDENTIFIER
%token <aCode>		QNAP_AND QNAP_OR QNAP_NOT QNAP_EQUAL QNAP_NOT_EQUAL QNAP_LESS QNAP_LESS_EQUAL QNAP_GREATER QNAP_GREATER_EQUAL
%token <aCode>		QNAP_POWER QNAP_PLUS QNAP_MINUS QNAP_DIVIDE QNAP_MULTIPLY QNAP_MODULUS
%token <aCode>		QNAP_BOOLEAN QNAP_REAL QNAP_INTEGER QNAP_STRING	QNAP_QUEUE QNAP_CLASS	/* variable types */
%token <aCode>		QNAP_CST QNAP_COX QNAP_EXP QNAP_ERLANG QNAP_HEXP			/* service distributions */
%token <aCode>		QNAP_SERVER QNAP_MULTIPLE QNAP_SINGLE QNAP_SOURCE QNAP_INFINITE		/* Station type */
%token <aReal>		DOUBLE
%token <aLong>		LONG

%type <aPointer>	class_list class_reference identifier_list object_list optional_init option_list optional_list service station_type transit transit_list transit_pair variable variable_list loop_list
%type <aPointer>	closed_statement compound_statement expression expression_list factor open_statement open_closed_statement power prefix_statement postfix_statement procedure_call relation simple_statement statement term
%type <aCode>		compound_station_type object_type simple_station_type variable_type
%type <aString>		identifier
%%

qnap2			: command_list QNAP_END_PROGRAM
			;

command_list		: command_list command
			| command
			;

command			: QNAP_CONTROL control_list
			| QNAP_DECLARE declare_list				{ qnap2_declare_objects(); }
			| QNAP_EXEC						{ qnap2_map_transit_to_visits(); }
			  statement						{ qnap2_set_program( $3 ); }
			| QNAP_REBOOT
			| QNAP_RESTART
			| QNAP_TERMINAL
			| QNAP_STATION station_list				{ qnap2_define_station(); }
			;

/* ------------------------------------------------------------------------ */
/*				   DECLARE				    */
/* ------------------------------------------------------------------------ */

declare_list		: declare_statement ';'
			| declare_list declare_statement ';'
			;

declare_statement	: QNAP_QUEUE variable_list				{ qnap2_declare_queue( $2 ); }
			| QNAP_QUEUE variable_type variable_list		{ qnap2_declare_attribute( $1, $2, $3 ); }
			| QNAP_CLASS variable_list				{ qnap2_declare_class( $2 ); }
			| QNAP_CLASS variable_type variable_list		{ qnap2_declare_attribute( $1, $2, $3 ); }
			| QNAP_REF object_type identifier_list			{ qnap2_declare_reference( $2, $3 ); }
			| variable_type variable_list				{ qnap2_declare_variable( $1, $2 ); }
			;

variable_type		: QNAP_INTEGER						{ $$ = $1; }
			| QNAP_REAL						{ $$ = $1; }
			| QNAP_BOOLEAN						{ $$ = $1; }
			| QNAP_STRING						{ $$ = $1; }
			;

variable_list		: variable						{ $$ = qnap2_append_pointer( NULL, $1 ); }
			| variable_list ',' variable				{ $$ = qnap2_append_pointer( $1, $3 ); }
			;

variable		: identifier optional_init				{ $$ = qnap2_define_variable( $1, NULL, NULL, $2 ); free( $1 ); }
			| identifier '(' factor ')' optional_init		{ $$ = qnap2_define_variable( $1, $3, NULL, $5 ); free( $1 ); }
			| identifier '(' factor ':' factor ')' optional_init	{ $$ = qnap2_define_variable( $1, $3, $5, $7 ); free( $1 ); }
			;

identifier_list		: identifier						{ $$ = qnap2_append_string( NULL, $1 ); free( $1 ); }
			| identifier_list ',' identifier			{ $$ = qnap2_append_string( $1, $3 ); free( $1 ); }
			;

optional_init		: QNAP_EQUAL factor					{ $$ = $2; }
			| 							{ $$ = NULL; }
			;

optional_list		: '(' loop_list ')'					{ $$ = $2; }
			| 							{ $$ = NULL; }
			;
/*
sublist			: simple_sublist
			| sublist ',' simple_sublist
			;

simple_sublist		: expression
			;
*/


/* ------------------------------------------------------------------------ */
/*				   STATION				    */
/* ------------------------------------------------------------------------ */

station_list		: station ';'
			| station_list station ';'
			;

station			: QNAP_INIT class_reference QNAP_EQUAL factor		{ qnap2_set_station_init( $2, $4 ); }
			| QNAP_NAME QNAP_EQUAL identifier optional_list		{ qnap2_set_station_name( $3 ); free( $3 ); }
			| QNAP_PRIO class_reference QNAP_EQUAL factor		{ qnap2_set_station_prio( $2, $4 ); }
			| QNAP_QUANTUM class_reference QNAP_EQUAL factor	{ qnap2_set_station_quantum( $2, $4 ); }
			| QNAP_RATE QNAP_EQUAL factor				{ qnap2_set_station_rate( $3 ); }
			| QNAP_SCHED QNAP_EQUAL identifier			{ qnap2_set_station_sched( $3 ); free( $3 ); }
			| QNAP_SERVICE class_reference QNAP_EQUAL service	{ qnap2_set_station_service( $2, $4 ); }
			| QNAP_TRANSIT class_reference QNAP_EQUAL transit	{ qnap2_set_station_transit( $2, $4 ); }
			| QNAP_TYPE QNAP_EQUAL station_type			{ qnap2_set_station_type( $3 ); free( $3 ); }
			;

class_reference		:							{ $$ = NULL; }
			| '(' QNAP_ALL QNAP_CLASS ')'				{ $$ = NULL; }
			| '(' class_list ')'					{ $$ = $2; }
			;

class_list		: identifier						{ $$ = qnap2_append_string( NULL, qnap2_get_class_name( $1 ) ); free( $1 ); }
			| class_list ',' identifier				{ $$ = qnap2_append_string( $1, qnap2_get_class_name( $3 ) ); free( $3 ); }
			;


service			: QNAP_CST '(' factor ')'				{ $$ = qnap2_get_service_distribution( $1, $3, NULL ); }
			| QNAP_COX '(' factor ')'				{ $$ = qnap2_get_service_distribution( $1, $3, NULL ); }
			| QNAP_EXP '(' factor ')'				{ $$ = qnap2_get_service_distribution( $1, $3, NULL ); }
			| QNAP_ERLANG '(' factor ',' factor ')'			{ $$ = qnap2_get_service_distribution( $1, $3, $5 ); }		/* $4 == c^2 */
			| QNAP_HEXP '(' factor ',' factor ')'			{ $$ = qnap2_get_service_distribution( $1, $3, $5 ); }		/* $4 == k == 1/c^2 */
			;

transit			: identifier						{ $$ = qnap2_append_pointer( NULL, qnap2_get_transit_pair( $1, NULL ) ); free( $1 ); }
			| transit_list						{ $$ = $1; }
			;

transit_list		: transit_pair						{ $$ = qnap2_append_pointer( NULL, $1 ); }
			| transit_list ',' transit_pair				{ $$ = qnap2_append_pointer( $1, $3 ); }
			;

transit_pair		: identifier optional_list ','
			  factor optional_list					{ $$ = qnap2_get_transit_pair( qnap2_get_station_name( $1 ), $4 ); free( $1 ); }
			;


station_type		: simple_station_type					{ $$ = qnap2_get_station_type_pair( $1, 1 ); }
			| QNAP_SOURCE						{ $$ = qnap2_get_station_type_pair( $1, 1 ); }
			| QNAP_MULTIPLE '(' QNAP_INTEGER ')'			{ $$ = qnap2_get_station_type_pair( $1, $3 ); }
			| compound_station_type					{ $$ = qnap2_get_station_type_pair( $1, 1 ); }
			| compound_station_type '(' QNAP_INTEGER ')'		{ $$ = qnap2_get_station_type_pair( $1, $3 ); }
			;

simple_station_type	: QNAP_INFINITE						{ $$ = $1; }
			| QNAP_SINGLE						{ $$ = $1; }
			;

compound_station_type	: QNAP_SERVER ',' simple_station_type			{ $$ = $1; }
			;

/* ------------------------------------------------------------------------ */
/* 				   CONTROL				    */
/* ------------------------------------------------------------------------ */

control_list		: control ';'
			| control_list control ';'
			;

control			: QNAP_CLASS QNAP_EQUAL QNAP_ALL QNAP_QUEUE
			| QNAP_OPTION QNAP_EQUAL option_list			{ qnap2_set_option( $3 ); }
			| QNAP_ENTRY QNAP_EQUAL open_closed_statement		{ qnap2_set_program( $3 ); }
			| QNAP_EXIT QNAP_EQUAL open_closed_statement		{ qnap2_set_program( $3 ); }
			;

option_list		: identifier						{ $$ = qnap2_append_string( NULL, $1 ); free( $1 ); }
			| option_list ',' identifier				{ $$ = qnap2_append_string( $1, $3 ); free( $3 ); }
			;

/* ------------------------------------------------------------------------ */
/* 				     EXEC				    */
/* ------------------------------------------------------------------------ */

statement		: open_closed_statement ';'				{ $$ = $1; }
			;

open_closed_statement	: open_statement					{ $$ = $1; }
			| closed_statement					{ $$ = $1; }
			;

open_statement		: QNAP_IF expression QNAP_THEN statement		{ $$ = qnap2_if_statement( $2, $4, NULL ); }
			| QNAP_IF expression QNAP_THEN closed_statement		/* -- */
			  QNAP_ELSE open_statement				{ $$ = qnap2_if_statement( $2, $4, $6 ); }
			| QNAP_WHILE expression QNAP_THEN open_statement	{ $$ = qnap2_while_statement( $2, $4 ); }
			| QNAP_FOR postfix_statement QNAP_ASSIGNMENT loop_list	/* -- */
			  QNAP_DO open_statement				{ $$ = qnap2_for_statement( $2, $4, $6 ); }
			| QNAP_FOR postfix_statement QNAP_ASSIGNMENT		/* -- */
			   QNAP_ALL object_list QNAP_DO open_statement		{ $$ = qnap2_foreach_statement( $2, $5, $7 ); }
			;

closed_statement	: simple_statement					{ $$ = $1; }
			| QNAP_IF expression QNAP_THEN closed_statement		/* -- */
			  QNAP_ELSE closed_statement				{ $$ = qnap2_if_statement( $2, $4, $6 ); }
			| QNAP_WHILE expression QNAP_THEN closed_statement	{ $$ = qnap2_while_statement( $2, $4 ); }
			| QNAP_FOR postfix_statement QNAP_ASSIGNMENT loop_list	/* -- */
			  QNAP_DO closed_statement				{ $$ = qnap2_for_statement( $2, $4, $6 ); }
			| QNAP_FOR postfix_statement QNAP_ASSIGNMENT 		/* -- */
			  QNAP_ALL object_list QNAP_DO closed_statement		{ $$ = qnap2_foreach_statement( $2, $5, $7 ); }
			;

simple_statement	: QNAP_BEGIN compound_statement QNAP_END		{ $$ = qnap2_compound_statement( $2 ); }
			| postfix_statement QNAP_ASSIGNMENT relation		{ $$ = qnap2_assignment( $1, $3 ); }
			| procedure_call					{ $$ = $1; }
			|							{ $$ = NULL; }
			;

compound_statement	: statement						{ $$ = qnap2_append_pointer( NULL, $1 ); }
			| compound_statement statement				{ $$ = qnap2_append_pointer( $1, $2 ); }
			;

procedure_call		: identifier						{ $$ = qnap2_get_procedure( $1, NULL ); free( $1 ); }
			| identifier '(' expression_list ')'			{ $$ = qnap2_get_procedure( $1, $3 ); free( $1 ); }
			;

relation		: expression						{ $$ = $1; }
			| relation QNAP_LESS expression				{ $$ = qnap2_relation( $2, $1, $3 ); }
			| relation QNAP_LESS_EQUAL expression			{ $$ = qnap2_relation( $2, $1, $3 ); }
			| relation QNAP_GREATER expression			{ $$ = qnap2_relation( $2, $1, $3 ); }
			| relation QNAP_GREATER_EQUAL expression		{ $$ = qnap2_relation( $2, $1, $3 ); }
			| relation QNAP_EQUAL expression			{ $$ = qnap2_relation( $2, $1, $3 ); }
			| relation QNAP_NOT_EQUAL expression			{ $$ = qnap2_relation( $2, $1, $3 ); }
			;

expression		: expression QNAP_PLUS term				{ $$ = qnap2_math( $2, $1, $3 ); }
			| expression QNAP_MINUS term				{ $$ = qnap2_math( $2, $1, $3 ); }
			| expression QNAP_OR term				{ $$ = qnap2_logic( $2, $1, $3 ); }
			| term							{ $$ = $1; }
			;

term			: term QNAP_MULTIPLY power				{ $$ = qnap2_math( $2, $1, $3 ); }
			| term QNAP_DIVIDE power				{ $$ = qnap2_math( $2, $1, $3 ); }
			| term QNAP_MODULUS power				{ $$ = qnap2_math( $2, $1, $3 ); }
			| term QNAP_AND power					{ $$ = qnap2_logic( $2, $1, $3 ); }
			| power							{ $$ = $1; }
			;

power			: prefix_statement QNAP_POWER power			{ $$ = qnap2_math( $2, $1, $3 ); }
			| prefix_statement					{ $$ = $1; }
			;

prefix_statement	: QNAP_PLUS postfix_statement				{ $$ = $2; }
			| QNAP_MINUS postfix_statement				{ $$ = qnap2_math( $1, NULL, $2 ); }
			| QNAP_NOT postfix_statement				{ $$ = qnap2_logic( $1, $2, NULL ); }
			| postfix_statement					{ $$ = $1; }
			;

postfix_statement	: identifier '(' expression_list ')'			{ $$ = qnap2_get_function( $1, $3 ); }
			| identifier '.' identifier				{ $$ = qnap2_get_attribute( qnap2_get_variable( $1 ), $3 ); free( $3 ); }
			| identifier '(' expression_list ')' '.' identifier	{ $$ = qnap2_get_attribute( qnap2_get_function( $1, $3 ), $6 ); free( $1 ); free( $3 ); free( $6 ); }
			| factor						{ $$ = $1; }
			;

factor			: identifier						{ $$ = qnap2_get_variable( $1 ); free( $1 ); }
			| LONG							{ $$ = qnap2_get_integer( $1 ); }
			| DOUBLE						{ $$ = qnap2_get_real( $1 ); }
			| STRING						{ $$ = qnap2_get_string( $1 ); free( $1 ); }
			| '(' relation ')'					{ $$ = $2; }
			;

expression_list		: expression						{ $$ = qnap2_append_pointer( NULL, $1 ); }
			| expression_list ',' expression			{ $$ = qnap2_append_pointer( $1, $3 ); }
			;

loop_list		: expression QNAP_STEP expression QNAP_UNTIL expression	{ $$ = qnap2_list( $1, $3, $5 ); }
			;

object_list		: object_type						{ $$ = qnap2_get_all_objects( $1 ); }
			;

object_type		: QNAP_QUEUE						{ $$ = $1; }
			| QNAP_CLASS						{ $$ = $1; }
			;


/* ------------------------------------------------------------------------ */

identifier		: QNAP_IDENTIFIER					{ $$ = $1; }
			| QNAP_CLASS						{ $$ = strdup( "class" ); }
			| QNAP_COX						{ $$ = strdup( "cox" ); }
			| QNAP_CST						{ $$ = strdup( "cst" ); }
			| QNAP_ERLANG						{ $$ = strdup( "erlang" ); }
			| QNAP_EXP						{ $$ = strdup( "exp" ); }
			| QNAP_HEXP						{ $$ = strdup( "hexp" ); }
			| QNAP_INFINITE						{ $$ = strdup( "infinite" ); }
			| QNAP_INIT						{ $$ = strdup( "init" ); }
			| QNAP_INTEGER						{ $$ = strdup( "integer" ); }
			| QNAP_MULTIPLE						{ $$ = strdup( "multiple" ); }
			| QNAP_NAME						{ $$ = strdup( "name" ); }
			| QNAP_OPTION						{ $$ = strdup( "option" ); }
			| QNAP_PRIO						{ $$ = strdup( "prio" ); }
			| QNAP_QUANTUM						{ $$ = strdup( "quantum" ); }
			| QNAP_QUEUE						{ $$ = strdup( "queue" ); }
			| QNAP_RATE						{ $$ = strdup( "rate" ); }
			| QNAP_REAL						{ $$ = strdup( "real" ); }
			| QNAP_SCHED						{ $$ = strdup( "sched" ); }
			| QNAP_SERVER						{ $$ = strdup( "server" ); }
			| QNAP_SERVICE						{ $$ = strdup( "service" ); }
			| QNAP_SINGLE						{ $$ = strdup( "single" ); }
			| QNAP_SOURCE						{ $$ = strdup( "source" ); }
			| QNAP_TRANSIT						{ $$ = strdup( "transit" ); }
			| QNAP_TYPE						{ $$ = strdup( "type" ); }
			;
%%
