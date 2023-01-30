/* -*- c++ -*-
 * $Id: jmva_document.cpp 16378 2023-01-29 13:05:35Z greg $
 *
 * Read in XML input files.
 *
 * Copyright the Real-Time and Distributed Systems Group,
 * Department of Systems and Computer Engineering,
 * Carleton University, Ottawa, Ontario, Canada. K1S 5B6
 *
 * ------------------------------------------------------------------------
 * December 2020.
 * ------------------------------------------------------------------------
 */

#define BUG_343 1
#define BUG_344 1
// undef UTILIZATION_BOUNDS

#if HAVE_CONFIG_H
#include <config.h>
#endif
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_SYS_MMAN_H
#include <sys/types.h>
#include <sys/mman.h>
#endif
#include <lqx/SyntaxTree.h>
#include <lqx/Program.h>
#include "bcmp_bindings.h"
#include "bcmp_to_lqn.h"
#include "common_io.h"
#include "dom_document.h"
#include "error.h"
#include "filename.h"
#include "glblerr.h"
#include "gnuplot.h"
#include "input.h"
#include "jmva_document.h"
#include "xml_input.h"
#include "xml_output.h"

extern "C" {
#include "srvn_gram.h"
}


namespace QNIO {
    /*
     * BSD complains about the implicit copy constructor.
     */

    JMVA_Document::Object::Object( const Object& o ) : _discriminator(o._discriminator)
    {
        switch ( _discriminator ) {
	case type::CLASS:   u.k = o.u.k; break;
	case type::DEMAND:  u.d = o.u.d; break;
	case type::MODEL:   u.m = o.u.m; break;
	case type::OBJECT:  u.o = o.u.o; break;
	case type::PAIR:    u.mk = o.u.mk; break;
	case type::STATION: u.s = o.u.s; break;
	case type::VOID:    u.v = nullptr; break;
	    /* NO default to catch new discriminators through warning */
	}
    }

    /* ---------------------------------------------------------------- */
    /* DOM input.                                                       */
    /* ---------------------------------------------------------------- */

    JMVA_Document::JMVA_Document( const std::string& input_file_name ) :
	Document( input_file_name, BCMP::Model() ),
	_strict_jmva(true), _parser(nullptr), _stack(),
	_lqx_program_text(), _lqx_program_line_number(0), _lqx(nullptr), _program(),
	_input_variables(), _whatif_body(), _independent_variables(), _result_variables(), _station_index(),
	_think_time_vars(), _population_vars(), _arrival_rate_vars(), _multiplicity_vars(), _service_time_vars(), _visit_vars(),
	_results(),
	_plot_customers(false), _N1(), _N2()
    {
    }

    JMVA_Document::JMVA_Document( const std::string& input_file_name, const BCMP::Model& model ) :
	Document( input_file_name, model ),
	_strict_jmva(true), _parser(nullptr), _stack(),
	_lqx_program_text(), _lqx_program_line_number(0), _lqx(nullptr), _program(),
	_input_variables(), _whatif_body(), _independent_variables(), _result_variables(), _station_index(),
	_think_time_vars(), _population_vars(), _arrival_rate_vars(), _multiplicity_vars(), _service_time_vars(), _visit_vars(),
	_results(),
	_plot_customers(false), _N1(), _N2()
    {
    }

    JMVA_Document::~JMVA_Document()
    {
    }

    /*
     * Load the document
     */

    bool
    JMVA_Document::load()
    {
	if ( !parse() ) return false;

	/* LQX present? */
	const std::string& program_text = getLQXProgramText();
	if ( !program_text.empty() ) {
	    _lqx = LQX::Program::loadFromText(getInputFileName().c_str(), getLQXProgramLineNumber(), program_text.c_str());
	}
	return true;
    }


    /*
     * For loading a JMVA document into an LQN document.  There is a
     * memory leak here, but the external variables are shared between
     * the jmva document and the dom.
     */

    bool
    JMVA_Document::load( LQIO::DOM::Document& lqn, const std::string& input_file_name )
    {
	JMVA_Document * jmva = new JMVA_Document( input_file_name );
	if ( !jmva->parse() ) return false;
	return  jmva->convertToLQN( lqn );
    }


    bool
    JMVA_Document::parse()
    {
	struct stat statbuf;
	bool rc = true;
	int input_fd = -1;

	if ( !LQIO::Filename::isFileName( getInputFileName() ) ) {
	    input_fd = fileno( stdin );
	} else if ( ( input_fd = open( getInputFileName().c_str(), O_RDONLY ) ) < 0 ) {
	    std::cerr << LQIO::io_vars.lq_toolname << ": Cannot open input file " << getInputFileName() << " - " << strerror( errno ) << std::endl;
	    return false;
	}

	if ( isatty( input_fd ) ) {
	    std::cerr << LQIO::io_vars.lq_toolname << ": Input from terminal is not allowed." << std::endl;
	    return false;
	} else if ( fstat( input_fd, &statbuf ) != 0 ) {
	    std::cerr << LQIO::io_vars.lq_toolname << ": Cannot stat " << getInputFileName() << " - " << strerror( errno ) << std::endl;
	    return false;
#if defined(S_ISSOCK)
	} else if ( !S_ISREG(statbuf.st_mode) && !S_ISFIFO(statbuf.st_mode) && !S_ISSOCK(statbuf.st_mode) ) {
#else
	} else if ( !S_ISREG(statbuf.st_mode) && !S_ISFIFO(statbuf.st_mode) ) {
#endif
	    std::cerr << LQIO::io_vars.lq_toolname << ": Input from " << getInputFileName() << " is not allowed." << std::endl;
	    return false;
	}

	_parser = XML_ParserCreateNS(NULL,'/');     /* Gobble header goop */
	if ( !_parser ) {
	    throw std::runtime_error("Could not allocate memory for Expat.");
	}

	XML_SetElementHandler( _parser, start, end );
	XML_SetCdataSectionHandler( _parser, start_cdata, end_cdata );
	XML_SetCharacterDataHandler( _parser, handle_text );
	XML_SetCommentHandler( _parser, handle_comment );
	XML_SetUnknownEncodingHandler( _parser, handle_encoding, static_cast<void *>(this) );
	XML_SetUserData( _parser, static_cast<void *>(this) );

	_stack.push( parse_stack_t("",&JMVA_Document::startDocument) );

#if HAVE_MMAP
	char *buffer;
#endif
	try {
#if HAVE_MMAP
	    buffer = static_cast<char *>(mmap( 0, statbuf.st_size, PROT_READ, MAP_PRIVATE|MAP_FILE, input_fd, 0 ));
	    if ( buffer != MAP_FAILED ) {
		if ( !XML_Parse( _parser, buffer, statbuf.st_size, true ) ) {
		    const char * error = XML_ErrorString(XML_GetErrorCode(_parser));
		    input_error( error );
		    rc = false;
		}
	    } else {
		/* Try the old way (for pipes) */
#endif
		const size_t BUFFSIZE = 1024;
		char buffer[BUFFSIZE];
		size_t len = 0;

		do {
		    len = read( input_fd, buffer, BUFFSIZE );
		    if ( static_cast<int>(len) < 0 ) {
			std::cerr << LQIO::io_vars.lq_toolname << ": Read error on " << getInputFileName() << " - " << strerror( errno ) << std::endl;
			rc = false;
			break;
		    } else if (!XML_Parse(_parser, buffer, len, len == 0 )) {
			const char * error = XML_ErrorString(XML_GetErrorCode(_parser));
			input_error( error );
			rc = false;
			break;
		    }
		} while ( len > 0 );
#if HAVE_MMAP
	    }
#endif
	}
	/* Halt on any error. */
	catch ( const XML::element_error& e ) {
	    input_error( "Unexpected element <%s> ", e.what() );
	    rc = false;
	}
	catch ( const std::runtime_error& e ) {
	    input_error( "Runtime error: %s ", e.what() );
	    rc = false;
	}

#if HAVE_MMAP
	if ( buffer != MAP_FAILED ) {
	    munmap( buffer, statbuf.st_size );
	}
#endif
	XML_ParserFree(_parser);
	_parser = 0;
	close( input_fd );
	return rc;
    }

    void
    JMVA_Document::input_error( const std::string& msg )
    {
	std::cerr << getInputFileName() << ":" << std::to_string(srvnlineno) << ": error: " << msg << std::endl;
    }

    void
    JMVA_Document::input_error( const std::string& msg, const std::string& arg )
    {
	std::cerr << getInputFileName() << ":" << std::to_string(srvnlineno) << ": error: " << msg << ": " << arg << std::endl;
    }

    /*
     * Handlers called from Expat.
     */

    void
    JMVA_Document::start( void *data, const XML_Char *el, const XML_Char **attr )
    {
	JMVA_Document * document = static_cast<JMVA_Document *>(data);
	srvnlineno = XML_GetCurrentLineNumber(document->_parser);
	if ( LQIO::DOM::Document::__debugXML ) {
	    std::cerr << std::setw(4) << srvnlineno << ": ";
	    for ( unsigned i = 0; i < document->_stack.size(); ++i ) {
		std::cerr << "  ";
	    }
	    std::cerr << "<" << el;
	    for ( const XML_Char ** attributes = attr; *attributes; attributes += 2 ) {
		std::cerr << " " << *attributes << "=\"" << *(attributes+1) << "\"";
	    }
	    std::cerr << ">" << std::endl;
	}
	try {
	    if ( document->_stack.size() > 0 ) {
		parse_stack_t& top = document->_stack.top();
		(document->*top.start)(top.object,el,attr);
	    }
	}
	catch ( const LQIO::duplicate_symbol& e ) {
	    LQIO::input_error2( LQIO::ERR_DUPLICATE_SYMBOL, el, e.what() );
	}
	catch ( const XML::missing_attribute & e ) {
	    LQIO::input_error2( LQIO::ERR_MISSING_ATTRIBUTE, el, e.what() );
	}
	catch ( const XML::unexpected_attribute & e ) {
	    LQIO::input_error2( LQIO::ERR_UNEXPECTED_ATTRIBUTE, el, e.what() );
	}
	catch ( const LQIO::undefined_symbol & e ) {
	    LQIO::input_error2( LQIO::ERR_NOT_DEFINED, e.what() );
	}
	catch ( const std::out_of_range& e ) {
	    document->input_error( "Undefined variable." );
	}
	catch ( const std::domain_error & e ) {
	    document->input_error( "Domain error: %s ", e.what() );
	}
	catch ( const std::invalid_argument & e ) {
	    LQIO::input_error2( LQIO::ERR_INVALID_ARGUMENT, el, e.what() );
	}
    }

    /*
     * Pop elements off the stack until we hit a matching tag.
     */

    void
    JMVA_Document::end( void *data, const XML_Char *el )
    {
	JMVA_Document * document = static_cast<JMVA_Document *>(data);
	bool done = false;
	while ( document->_stack.size() > 0 && !done ) {
	    parse_stack_t& top = document->_stack.top();
	    if ( LQIO::DOM::Document::__debugXML ) {
		std::cerr << std::setw(4) << srvnlineno << ": ";
		for ( unsigned i = 1; i < document->_stack.size(); ++i ) {
		    std::cerr << "  ";
		}
		if ( top.element.size() ) {
		    std::cerr << "</" << top.element << ">" << std::endl;
		} else {
		    std::cerr << "empty stack" << std::endl;
		}
	    }
	    done = top == el;
	    if ( top.end ) {		/* Run the end handler if necessary */
		(document->*top.end)(top.object,el);
	    }
	    document->_stack.pop();
	}
    }

    void
    JMVA_Document::start_cdata( void * data )
    {
    }


    void
    JMVA_Document::end_cdata( void * data )
    {
    }


    /*
     * Ignore most text.  However, for an LQX program, concatenate
     * the text.  Since expat gives us text in "chunks", we can't
     * just simply call setLQXProgram.  Rather, we "append" the
     * program to the existing one.
     */

    void
    JMVA_Document::handle_text( void * data, const XML_Char * text, int length )
    {
	const static std::vector<JMVA_Document::start_fptr> text_handler = {
	    &JMVA_Document::startDescription,
	    &JMVA_Document::startServiceTime,
	    &JMVA_Document::startServiceTimeList,		/* BUG_411 */
	    &JMVA_Document::startVisit
	};
	
	JMVA_Document * parser = static_cast<JMVA_Document *>(data);
	if ( parser->_stack.size() == 0 ) return;
	const parse_stack_t& top = parser->_stack.top();
	if ( std::find( text_handler.begin(), text_handler.end(), top.start ) !=  text_handler.end() ) {
	    parser->_text.append( text, length );
	} else if ( top.start == &JMVA_Document::startLQX ) {
	    std::string& program = const_cast<std::string &>(parser->getLQXProgramText());
	    program.append( text, length );
	}
    }

    /*
     * We tack the comment onto the current element.
     */

    void
    JMVA_Document::handle_comment( void * data, const XML_Char * text )
    {
	JMVA_Document * parser = static_cast<JMVA_Document *>(data);
	if ( parser->_stack.size() == 0 ) return;
	const parse_stack_t& top = parser->_stack.top();
	if ( top.object.isObject() ) {
	    BCMP::Model::Object * object = top.object.getObject();
	    std::string& comment = const_cast<std::string&>(object->getComment());
	    if ( comment.size() ) {
		comment += "\n";
	    }
	    comment += text;
	}
    }

    int
    JMVA_Document::handle_encoding( void * data, const XML_Char *name, XML_Encoding *info )
    {
	if ( strcasecmp( name, "ascii" ) == 0 ) {
	    /* Initialize the info argument to handle plain old ascii. */
	    for ( unsigned int i = 0; i < 256; ++i ) {
		info->map[i] = i;
	    }
	    info->convert = 0;		/* No need as its all 1 to 1. */
	    info->data = data;		/* The data argument is a pointer to the current document. */
	    return XML_STATUS_OK;
	} else {
	    return XML_STATUS_ERROR;
	}
    }

    bool
    JMVA_Document::parse_stack_t::operator==( const XML_Char * str ) const
    {
	return element == str;
    }

    bool
    JMVA_Document::checkAttributes( const XML_Char * element_name, const XML_Char ** attributes, const std::set<const XML_Char *,JMVA_Document::attribute_table_t>& table ) const
    {
	bool rc = true;
	for ( ; *attributes; attributes += 2 ) {
	    std::set<const XML_Char *>::const_iterator item = table.find(*attributes);
	    if ( item == table.end() ) {
		if ( strncasecmp( *attributes, "http:", 5 ) != 0 ) {                /* Skip these */
		    LQIO::input_error2( LQIO::ERR_UNEXPECTED_ATTRIBUTE, element_name, *attributes );
		    rc = false;
		}
	    }
	}
	return rc;
    }


    void
    JMVA_Document::registerExternalSymbolsWithProgram(LQX::Program* program)
    {
	std::for_each( _input_variables.begin(), _input_variables.end(), register_variable( program ) );
    }

    void
    JMVA_Document::register_variable::operator()( const std::string& variable ) const
    {
	_lqx->defineExternalVariable( variable );
    }


    /*
     * Save the results from iteration i, station m, class k
     */
     
    void
    JMVA_Document::saveResults( size_t i, const std::string& solver, size_t iterations, const std::string& m, const std::string& k, const std::map<BCMP::Model::Result::Type,double>& results )
    {
	/* The actual data... */
	auto& m_map = _results.emplace( i, std::map<const std::string,std::map<const std::string,std::map<BCMP::Model::Result::Type,double>>>() ).first->second;	// gets the map.
	auto& k_map = m_map.emplace( m, std::map<const std::string,std::map<BCMP::Model::Result::Type,double>>() ).first->second;	// gets the map.
	k_map.emplace( k, results );	// saves the data.

	/* auxiliary info */
	_mva_info.emplace( i, std::pair<const std::string,size_t>(solver,iterations) );
    }

    /* ---------------------------------------------------------------- */
    /* Parser functions.                                                */
    /* ---------------------------------------------------------------- */

    void
    JMVA_Document::startDocument( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	static const std::set<const XML_Char *,JMVA_Document::attribute_table_t> document_table = { Xxml_debug, Xjaba };

	if ( strcasecmp( element, Xmodel ) == 0 ) {
	    checkAttributes( element, attributes, document_table );
	    LQIO::DOM::Document::__debugXML = (LQIO::DOM::Document::__debugXML || XML::getBoolAttribute(attributes,Xxml_debug));
	    _stack.push( parse_stack_t(element,&JMVA_Document::startModel,&JMVA_Document::endModel,object) );
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }

    void
    JMVA_Document::startModel( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	static const std::set<const XML_Char *,JMVA_Document::attribute_table_t> solutions_table = { XalgCount, Xiteration, XiterationValue, Xok, XsolutionMethod };

	if ( strcasecmp( element, Xpragma ) == 0 ) {
	    insertPragma( XML::getStringAttribute(attributes,Xparam), XML::getStringAttribute(attributes,Xvalue,"") );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startNOP) );
	} else if ( strcasecmp( element, Xdescription ) == 0 ) {
	    checkAttributes( element, attributes, null_table );
	    _text.clear();						// Reset text buffer.
	    _stack.push( parse_stack_t(element,&JMVA_Document::startDescription,&JMVA_Document::endDescription,Object(&model())) );
	} else if ( strcasecmp( element, Xparameters ) == 0 ) {
	    checkAttributes( element, attributes, null_table );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startParameters) );
	} else if ( strcasecmp( element, XalgParams) == 0 ) {
	    checkAttributes( element, attributes, null_table );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startAlgParams) );
	} else if ( strcasecmp( element, XwhatIf ) == 0 ) {
	    static const std::set<const XML_Char *,JMVA_Document::attribute_table_t> whatIf_table = { XclassName, XstationName, Xtype, Xvalues };
	    checkAttributes( element, attributes, whatIf_table );
	    createWhatIf( attributes );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startNOP) );
	} else if ( strcasecmp( element, Xsolutions ) == 0 ) {
	    checkAttributes( element, attributes, solutions_table );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startSolutions) );
	} else if ( strcasecmp( element, XLQX ) == 0 ) {
	    setLQXProgramLineNumber(XML_GetCurrentLineNumber(_parser));
	    _stack.push( parse_stack_t(element,&JMVA_Document::startLQX) );
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }


    /*
     * If I have a Whatif, but no results, create them
     */

    void
    JMVA_Document::endModel( Object& object, const XML_Char * element )
    {
	if ( _result_variables.empty() ) defineDefaultResultVariables();

    }


    void JMVA_Document::startDescription( Object&, const XML_Char * element, const XML_Char ** attributes )
    {
	XML::throw_element_error( element, attributes );           	/* Should not get here. */
    }


    void JMVA_Document::endDescription( Object& object, const XML_Char * element )
    {
	// Through the magic of unions...
	object.getModel()->insertComment(LQIO::rtrim(LQIO::ltrim(_text)));
    }


    /*
     * <parameters>
     *   <classes number="1">
     *   <stations number="3">
     *   <ReferenceStation number="1">
     * </parameters>
     */

    void
    JMVA_Document::startParameters( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	static std::set<const XML_Char *,JMVA_Document::attribute_table_t> parameter_table = { Xnumber };

	checkAttributes( element, attributes, parameter_table );		// Hoist: All elements the same
	if ( strcasecmp( element, Xclasses ) == 0 ) {
	    _stack.push( parse_stack_t(element,&JMVA_Document::startClasses) );
	} else if ( strcasecmp( element, Xstations ) == 0 ) {
	    _stack.push( parse_stack_t(element,&JMVA_Document::startStations) );
	} else if ( strcasecmp( element, XReferenceStation ) == 0 ) {
	    _stack.push( parse_stack_t(element,&JMVA_Document::startReferenceStation) );
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }


    /*
     * <classes number="1">
     *   <closedclass name="c1" population="4"/>
     * </classes>
     */

    void
    JMVA_Document::startClasses( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	if ( strcasecmp( element, Xclosedclass) == 0 ) {
	    static const std::set<const XML_Char *,JMVA_Document::attribute_table_t> closedclass_table = { Xname, Xpopulation, Xthinktime };
	    checkAttributes( element, attributes, closedclass_table );
	    createClosedChain( attributes );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startNOP) );
	} else if ( strcasecmp( element, Xopenclass) == 0 ) {
	    static const std::set<const XML_Char *,JMVA_Document::attribute_table_t> openclass_table = { Xname, Xrate };
	    checkAttributes( element, attributes, openclass_table );
	    createOpenChain( attributes );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startNOP) );
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }


    /*
     * <stations number="3">
     *   <delaystation name="Reference">...
     *   <listation name="p2">...
     * </stations>
     */

    void
    JMVA_Document::startStations( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	static const std::set<const XML_Char *,JMVA_Document::attribute_table_t> station_table = { Xname, Xservers };
	checkAttributes( element, attributes, station_table );	// Hoist.  Common to all stations.
	if ( strcasecmp( element, Xdelaystation ) == 0 ) {
	    _stack.push( parse_stack_t(element,&JMVA_Document::startStation,Object(createStation( BCMP::Model::Station::Type::DELAY, attributes )) ) );
	} else if ( strcasecmp( element, Xlistation ) == 0 ) {
	    _stack.push( parse_stack_t(element,&JMVA_Document::startStation,Object(createStation( BCMP::Model::Station::Type::LOAD_INDEPENDENT, attributes )) ) );
	} else if ( strcasecmp( element, Xldstation ) == 0 ) {
	    _stack.push( parse_stack_t(element,&JMVA_Document::startStation,Object(createStation( BCMP::Model::Station::Type::MULTISERVER, attributes )) ) );
	} else { // multiserver???
	    XML::throw_element_error( element, attributes );
	}
    }

    /*
     * <listation name="p3">
     *   <servicetimes>
     *     <servicetime customerclass="c1">1</servicetime>
     *   </servicetimes>
     *   <visits>
     *     <visits customerclass="c1">1</visits>
     *   </visits>
     * </listation>
     */

    void
    JMVA_Document::startStation( Object& station, const XML_Char * element, const XML_Char ** attributes )
    {
	checkAttributes( element, attributes, null_table );
	if ( strcasecmp( element, Xservicetimes ) == 0 ) {
	    _stack.push( parse_stack_t(element,&JMVA_Document::startServiceTimes,station) );
	} else if ( strcasecmp( element, Xvisits ) == 0 ) {
	    _stack.push( parse_stack_t(element,&JMVA_Document::startVisits,station) );
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }

    /* Place holder for handle_text */

    const std::set<const XML_Char *,JMVA_Document::attribute_table_t> JMVA_Document::demand_table = { Xcustomerclass };

    void
    JMVA_Document::startServiceTimes( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	checkAttributes( element, attributes, demand_table );	// common to visits
	BCMP::Model::Station * station = object.getStation();
	assert( station != nullptr );
	std::string class_name = XML::getStringAttribute( attributes, Xcustomerclass );
	if ( strcasecmp( element, Xservicetime ) == 0 ) {
	    _text.clear();					// Reset buffer for handle_text.
	    BCMP::Model::Station::Class::map_t& demands = station->classes();		// Will insert...
	    const std::pair<BCMP::Model::Station::Class::map_t::iterator,bool> result = demands.emplace( class_name, BCMP::Model::Station::Class() );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startServiceTime,&JMVA_Document::endServiceTime,Object(&result.first->second) ) );
	} else if ( strcasecmp( element, Xservicetimes ) == 0 ) {	// BUG_411 -- pass in station object
	    _text.clear();					// Reset buffer for handle_text.
	    BCMP::Model::Station::Class::map_t& demands = station->classes();		// Will insert...
	    const std::pair<BCMP::Model::Station::Class::map_t::iterator,bool> result = demands.emplace( class_name, BCMP::Model::Station::Class() );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startServiceTimeList,&JMVA_Document::endServiceTimeList,Object(Object::MK(station,&result.first->second)) ) );
	} else {
	    XML::throw_element_error( element, attributes );      	/* Should not get here. */
	}
    }

    void
    JMVA_Document::startServiceTime( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	XML::throw_element_error( element, attributes );           	/* Should not get here. */
    }

    void
    JMVA_Document::endServiceTime( Object& object, const XML_Char * element )
    {
	LQX::SyntaxTreeNode * service_time = getVariable( element, _text.c_str() );
	object.getDemand()->setServiceTime( service_time );			// Through the magic of unions...
	if ( dynamic_cast<LQX::VariableExpression *>(service_time) ) _service_time_vars.emplace(object.getDemand(),dynamic_cast<LQX::VariableExpression *>(service_time)->getName());
    }

    /*+ BUG_411
     * For LIStation... (FESC)
     */

    void
    JMVA_Document::startServiceTimeList( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	XML::throw_element_error( element, attributes );           	/* Should not get here. */
    }

    /*+ BUG_411
     * For LDStation... (FESC).  We haven't implemented this type, but
     * JMVA doesn't implement 'servers=' attribute (and it the number
     * of service times has to equal the number of customers).  So,
     * take only the first one, then ignore the others.  Count the
     * unique items to determine the number of servers.  JMVA insists
     * that n==1 for the "servers=<n>" attribute.
     */

    void
    JMVA_Document::endServiceTimeList( Object& object, const XML_Char * element )
    {
	Object::MK& mk = object.getMK();	/* Station,class pair */
	BCMP::Model::Station * m = const_cast<BCMP::Model::Station *>(mk.first);
	BCMP::Model::Station::Class * k = const_cast<BCMP::Model::Station::Class *>(mk.second);

	/* Tokeninze the input string on ';' */
	std::string& values = _text;
	const char delim = ';';
	size_t start;
	size_t finish = 0;
	size_t count = 0;
	std::string last_value = "";
	while ( (start = values.find_first_not_of( delim, finish )) != std::string::npos ) {
	    finish = values.find(delim, start);
	    const std::string value = values.substr( start, finish - start  );
	    if ( value != last_value ) {
		last_value = value;
		count += 1;
	    }
	    if ( count == 1 ) {
		/* this is the service time at one customer, which we need */
		LQX::SyntaxTreeNode * service_time = getVariable( element, value.c_str() );
		k->setServiceTime( service_time );
		if ( dynamic_cast<LQX::VariableExpression *>(service_time) ) _service_time_vars.emplace(k,dynamic_cast<LQX::VariableExpression *>(service_time)->getName());
	    }
	}
	m->setCopies( BCMP::Model::max( m->copies(), new LQX::ConstantValueExpression( static_cast<double>(count) ) ) );
    }

    void
    JMVA_Document::startVisits( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	checkAttributes( element, attributes, demand_table );	// commmon to servicetime.
	BCMP::Model::Station * station = object.getStation();
	assert( station != nullptr );
	std::string class_name = XML::getStringAttribute( attributes, Xcustomerclass );
	if ( strcasecmp( element, Xvisit ) == 0 ) {
	    _text.clear();					// Reset buffer for handle_text.
	    BCMP::Model::Station::Class::map_t& demands = station->classes();		// Will insert...
	    const std::pair<BCMP::Model::Station::Class::map_t::iterator,bool> result = demands.emplace( class_name, BCMP::Model::Station::Class() );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startVisit,&JMVA_Document::endVisit,Object(&result.first->second) ) );
	} else {
	    XML::throw_element_error( element, attributes );       	/* Should not get here. */
	}
    }

    /* Place holder for handle_text */

    void
    JMVA_Document::startVisit( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	XML::throw_element_error( element, attributes );           	/* Should not get here. */
    }


    void JMVA_Document::endVisit( Object& object, const XML_Char * element )
    {
	LQX::SyntaxTreeNode * visits = getVariable( element, _text.c_str() );
	object.getDemand()->setVisits( visits );				// Through the magic of unions...
	if ( dynamic_cast<LQX::VariableExpression *>(visits) ) _visit_vars.emplace(object.getDemand(),dynamic_cast<LQX::VariableExpression *>(visits)->getName());
    }

    /*
     * <ReferenceStation number="1">
     *   <Class name="c1" refStation="Reference"/>
     * </ReferenceStation>
     */

    void
    JMVA_Document::startReferenceStation( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	static const std::set<const XML_Char *,JMVA_Document::attribute_table_t> ReferenceStation_table = { Xname, XrefStation };
	if ( strcasecmp( element, XClass ) == 0 ) {
	    checkAttributes( element, attributes, ReferenceStation_table );
	    const std::string refStation = XML::getStringAttribute( attributes, XrefStation );
	    if ( refStation != XArrivalProcess ) {
		model().stationAt( refStation ).setReference(true);
	    }
	    _stack.push( parse_stack_t(element,&JMVA_Document::startNOP,object) );
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }


    /*
     * <algParams>
     *   <algType maxSamples="10000" name="MVA" tolerance="1.0E-7"/>
     *   <compareAlgs value="false"/>
     * </algParams>
     */

    void
    JMVA_Document::startAlgParams( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	if ( strcasecmp( element, XalgType ) == 0 ) {
	    _stack.push( parse_stack_t(element,&JMVA_Document::startNOP,object) );
	} else if ( strcasecmp( element, XcompareAlgs ) == 0 ) {
	    _stack.push( parse_stack_t(element,&JMVA_Document::startNOP,object) );
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }

    /*
     * <solutions algCount="1" iteration="0" iterationValue="1.0" ok="true" solutionMethod="analytical whatif">
     *   <algorithm iterations="0" name="MVA">
     *     <stationresults station="terminals">
     *       <classresults customerclass="c1">
     *         <measure meanValue="0.8683417085427135" measureType="Number of Customers" successful="true"/>
     *         <measure meanValue="0.8683417085427135" measureType="Throughput" successful="true"/>
     *         <measure meanValue="1.0" measureType="Residence time" successful="true"/>
     *         <measure meanValue="0.8683417085427135" measureType="Utilization" successful="true"/>
     *       </classresults>
     */

    void
    JMVA_Document::startSolutions( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	if ( strcasecmp( element, Xalgorithm ) == 0 ) {
	    static const std::set<const XML_Char *,JMVA_Document::attribute_table_t> table = { Xiterations, Xname };
	    checkAttributes( element, attributes, table );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startAlgorithm,object) );
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }

    void
    JMVA_Document::startAlgorithm( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	if ( strcasecmp( element, Xstationresults ) == 0 ) {
	    static const std::set<const XML_Char *,JMVA_Document::attribute_table_t> station_results_table = {
		Xstation
	    };

	    checkAttributes( element, attributes, station_results_table );
	    const std::string name = XML::getStringAttribute(attributes,Xstation);
	    Object mk_object(Object::MK(&model().stationAt(name),nullptr));
	    _stack.push( parse_stack_t(element,&JMVA_Document::startStationResults,mk_object) );
	} else if ( strcasecmp( element, Xnormconst ) == 0 ) {
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }

    const std::set<const XML_Char *,JMVA_Document::attribute_table_t> JMVA_Document::measure_table = {
	XmeanValue,
	XmeasureType,
	Xsuccessful
    };

    void
    JMVA_Document::startStationResults( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	const std::set<const XML_Char *,JMVA_Document::attribute_table_t> class_results_table = {
	    Xcustomerclass,
	};

	Object::MK& mk = object.getMK();
	if ( strcasecmp( element, Xclassresults ) == 0 ) {
	    checkAttributes( element, attributes, class_results_table );
	    const std::string name = XML::getStringAttribute(attributes,Xcustomerclass);
	    const BCMP::Model::Station * m = mk.first;
	    mk.second = &m->classAt(name);
	    _stack.push( parse_stack_t(element,&JMVA_Document::startClassResults,object) );
	} else if ( strcasecmp( element, Xmeasure ) == 0 ) {
	    checkAttributes( element, attributes, measure_table );
	    mk.second = nullptr;
	    createMeasure( object, attributes );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startNOP,object) );
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }


    /*
     * Generate SPEX result vars here.
     */

    void
    JMVA_Document::startClassResults( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	if ( strcasecmp( element, Xmeasure ) == 0 ) {
	    checkAttributes( element, attributes, measure_table );
	    createMeasure( object, attributes );
	    _stack.push( parse_stack_t(element,&JMVA_Document::startNOP,object) );
	} else {
	    XML::throw_element_error( element, attributes );
	}
    }


    /*
     */

    void
    JMVA_Document::startLQX( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	XML::throw_element_error( element, attributes );             /* Should not get here. */
    }


    /*
     */

    void
    JMVA_Document::startNOP( Object& object, const XML_Char * element, const XML_Char ** attributes )
    {
	XML::throw_element_error( element, attributes );             /* Should not get here. */
    }


    /*
     * Return either a constant or a variable by converting attribute.  If the attribute is NOT found
     * use the default (if present), or throw missing_attribute.
     */

    LQX::SyntaxTreeNode *
    JMVA_Document::getVariableAttribute( const XML_Char **attributes, const XML_Char * attribute, double default_value )
    {
	for ( ; *attributes; attributes += 2 ) {
	    if ( strcasecmp( *attributes, attribute ) == 0 ) return getVariable( attribute, *(attributes+1) );
	}
	if ( default_value >= 0.0 ) {
	    return new LQX::ConstantValueExpression( default_value );
	} else {
	    throw XML::missing_attribute( attribute );
	}
    }

    LQX::SyntaxTreeNode *
    JMVA_Document::getVariable( const XML_Char *attribute, const XML_Char *value )
    {
	if ( value[0] == '$' ) {
	    _input_variables.emplace(value);
	    return new LQX::VariableExpression(value,true);	// Always create a new value because LQX will delete them all.
	} else {
	    char* endPtr = nullptr;
	    const char* realEndPtr = value + strlen(value);
	    const double real = strtod(value, &endPtr);
	    if ( endPtr != realEndPtr ) throw std::invalid_argument(value);
	    return new LQX::ConstantValueExpression(real);
	}
    }

    double
    JMVA_Document::getDoubleValue( LQX::SyntaxTreeNode * value ) const
    {
	if ( value == nullptr ) return 0.;
	return value->invoke( getLQXEnvironment() )->getDoubleValue();
    }

    void
    JMVA_Document::createClosedChain( const XML_Char ** attributes )
    {
	std::string name = XML::getStringAttribute( attributes, Xname );
	LQX::SyntaxTreeNode * population = getVariableAttribute( attributes, Xpopulation );
	LQX::SyntaxTreeNode * think_time = getVariableAttribute( attributes, Xthinktime, 0.0 );
	std::pair<BCMP::Model::Chain::map_t::iterator,bool> result = model().insertClosedChain( name, population, think_time );
	if ( !result.second ) throw std::runtime_error( "Duplicate class" );
	if ( dynamic_cast<LQX::VariableExpression *>(population) ) _population_vars.emplace(&result.first->second,dynamic_cast<LQX::VariableExpression *>(population)->getName());
	if ( dynamic_cast<LQX::VariableExpression *>(think_time) ) _think_time_vars.emplace(&result.first->second,dynamic_cast<LQX::VariableExpression *>(think_time)->getName());
    }


    void
    JMVA_Document::createOpenChain( const XML_Char ** attributes )
    {
	std::string name = XML::getStringAttribute( attributes, Xname );
	LQX::SyntaxTreeNode * arrival_rate = getVariableAttribute( attributes, Xrate );
	std::pair<BCMP::Model::Chain::map_t::iterator,bool> result = model().insertOpenChain( name, arrival_rate );
	if ( !result.second ) throw std::runtime_error( "Duplicate class" );
	if ( dynamic_cast<LQX::VariableExpression *>(arrival_rate) ) _arrival_rate_vars.emplace(&result.first->second,dynamic_cast<LQX::VariableExpression *>(arrival_rate)->getName());
    }


    BCMP::Model::Station *
    JMVA_Document::createStation( BCMP::Model::Station::Type type, const XML_Char ** attributes )
    {
	scheduling_type scheduling = SCHEDULE_DELAY;
	switch ( type ) {
	case BCMP::Model::Station::Type::LOAD_INDEPENDENT: scheduling = SCHEDULE_PS; break;
	case BCMP::Model::Station::Type::MULTISERVER: scheduling = SCHEDULE_PS; break;
	default: break;
	}
	const std::string name = XML::getStringAttribute( attributes, Xname );
	LQX::SyntaxTreeNode * multiplicity = getVariableAttribute( attributes, Xservers, 1 );
	const std::pair<BCMP::Model::Station::map_t::iterator,bool> result = model().insertStation( name, BCMP::Model::Station( type, scheduling, multiplicity ) );
	if ( !result.second ) throw std::runtime_error( "Duplicate station" );
	BCMP::Model::Station * station = &result.first->second;

	if ( type == BCMP::Model::Station::Type::MULTISERVER ) {
	    if ( dynamic_cast<LQX::VariableExpression *>(multiplicity) ) _multiplicity_vars.emplace(station,dynamic_cast<LQX::VariableExpression *>(multiplicity)->getName());
	} else if ( !BCMP::Model::isDefault( getVariableAttribute( attributes, Xservers, 1 ), 1.0 ) ) {
	    /* Only multiservers can have servers != 1 */
	    runtime_error( LQIO::ERR_INVALID_PARAMETER, Xservers, Xlistation, XML::getStringAttribute( attributes, Xname ), "Not equal to 1" );
	}
	return station;
    }

    /*
     * <whatIf type="Customer Numbers" values="1.0;1.5;2.0"/>  <!-- all classes by ratio -->
     * <whatIf className="c1" type="Population Mix" values="0.16666666666666666;0.3333333333333333;0.5;0.6666666666666666;0.8333333333333333"/>
     * <whatIf className="c1" type="Customer Numbers" values="4.0;5.0;6.0;7.0;8.0"/>
     * <whatIf className="c1" stationName="p1" type="Service Demands" values="0.4;0.44000000000000006;0.4800000000000001;0.5200000000000001;0.56;0.6;0.64;0.68;0.72;0.76;0.8"/>
     * <whatIf stationName="p1" type="Service Demands" values="1.0;1.1;1.2;1.3;1.4;1.5;1.6;1.7;1.8;1.9;2.0"/>  <!-- all classes by ratio -->
     * values are generators, but enumerated.
     *
     * If only a className XOR a station name is present, then apply
     * proportinately to all classes or stations, otherwise assign
     * values.  We can be a bit more flexible.  The values are a list
     * which can be converted to a comprehension.  The latter is better
     * for QNAP2 output.
     *
     * type can be "Customer Numbers" or "Service Demands" (or
     * "Population Mix" - ratio between two classes)
     */

    const std::map<const std::string,JMVA_Document::setIndependentVariable> JMVA_Document::independent_var_table = {
	{ XArrival_Rates, 	&JMVA_Document::setArrivalRate },
	{ XCustomer_Numbers, 	&JMVA_Document::setCustomers },
	{ XNumber_of_Servers, 	&JMVA_Document::setMultiplicity },
	{ XPopulation_Mix,	&JMVA_Document::setPopulationMix },
	{ XService_Demands, 	&JMVA_Document::setDemand }
    };

    void
    JMVA_Document::createWhatIf( const XML_Char ** attributes )
    {
	const std::string className = XML::getStringAttribute( attributes, XclassName, "" );		/* className and/or stationName */
	const std::string stationName = XML::getStringAttribute( attributes, XstationName, "" );	/* className and/or stationName */

	const std::string x_label = XML::getStringAttribute( attributes, Xtype );
	std::map<const std::string,JMVA_Document::setIndependentVariable>::const_iterator f = independent_var_table.find( x_label );
	if ( f == independent_var_table.end() ) throw std::runtime_error( "JMVA_Document::createWhatIf" );

	const bool is_unsigned = (f->second == &JMVA_Document::setMultiplicity || f->second == &JMVA_Document::setCustomers);
	const std::string x_var = (this->*(f->second))( stationName, className );
	const Comprehension comprehension( x_var, XML::getStringAttribute( attributes, Xvalues ), is_unsigned );

	_independent_variables.emplace_back( x_var );
	appendResultVariable( x_var, new LQX::VariableExpression( x_var, false ) );

	if ( comprehension.begin() != comprehension.end() ) {
	    if ( f->second == &JMVA_Document::setPopulationMix ) {
		setPopulationMixN1N2( className, comprehension );
	    }
	    insertComprehension( comprehension );
	}
    }



    /*
     * Set the result variables.  They can be either at the Station or at the Class within the station.
     *
     *	<measure meanValue="1.0" measureType="Number of Customers" successful="true"/>
     *  <measure meanValue="1.0" measureType="Throughput" successful="true"/>
     *	<measure meanValue="1.0" measureType="Residence time" successful="true"/>
     *  <measure meanValue="1.0" measureType="Utilization" successful="true"/>
     */

    void
    JMVA_Document::createMeasure( Object& object, const XML_Char ** attributes )
    {
	static const std::map<const std::string,const BCMP::Model::Result::Type> measure_table = {
	    {XNumber_of_Customers,  BCMP::Model::Result::Type::QUEUE_LENGTH},
	    {XThroughput, 	    BCMP::Model::Result::Type::THROUGHPUT},
	    {XResidence_Time, 	    BCMP::Model::Result::Type::RESIDENCE_TIME},
	    {XUtilization, 	    BCMP::Model::Result::Type::UTILIZATION}
	};

	std::string value = XML::getStringAttribute( attributes, XmeanValue );
	if ( !value.empty() && value[0] == '$' ) {
	    const BCMP::Model::Station * m = const_cast<BCMP::Model::Station*>(object.getMK().first);
	    const BCMP::Model::Station::Class * k = const_cast<BCMP::Model::Station::Class *>(object.getMK().second);

	    createObservation( value, measure_table.at(XML::getStringAttribute( attributes, XmeasureType ) ), m, k );
	}
    }

    std::string
    JMVA_Document::setArrivalRate( const std::string& stationName, const std::string& className )
    {
	BCMP::Model::Chain& k = chains().at(className);
	std::string name;
	const std::map<const BCMP::Model::Chain *,std::string>::const_iterator var = _arrival_rate_vars.find( &k );		/* chain, var	*/
	if ( var != _arrival_rate_vars.end() ) {
	    name = var->second;
	} else {
	    name = "$A" + std::to_string(_arrival_rate_vars.size() + 1);
	    _arrival_rate_vars.emplace( &k, name );
	    _input_variables.emplace( name );
	}
	k.setArrivalRate( new LQX::VariableExpression( name, false ) );
	return name;
    }


    std::string
    JMVA_Document::setCustomers( const std::string& stationName, const std::string& className )
    {
	setPlotCustomers( true );

	BCMP::Model::Chain& k = chains().at(className);
	std::string name;

	/* Get a variable... $N1,$N2,... */
	std::map<const BCMP::Model::Chain *,std::string>::iterator var = _population_vars.find(&k);	/* Look for class 		*/
	if ( var != _population_vars.end() ) {							/* Var is defined for class	*/
	    name = var->second;
	} else {
	    name = "$N" + std::to_string(_population_vars.size() + 1);				/* Need to create one 		*/
	    _population_vars.emplace( &k, name );
	    _input_variables.emplace( name );							/* Save it.			*/
	}
	k.setCustomers( new LQX::VariableExpression( name, false ) );				/* swap constanst for variable in class */
	return name;
    }

    std::string
    JMVA_Document::setDemand( const std::string& stationName, const std::string& className )
    {
	BCMP::Model::Station& m = stations().at(stationName);
	std::string name;
	if ( className.empty() ) abort();

	BCMP::Model::Station::Class& d = m.classes().at(className);
	/* Get a variable... $S1,$S2,... */
	std::map<const BCMP::Model::Station::Class *,std::string>::iterator var = _service_time_vars.find( &d );
	if ( var != _service_time_vars.end() ) {
	    name = var->second;
	} else {
	    name = "$S" + std::to_string(_service_time_vars.size() + 1);
	    _service_time_vars.emplace( &d, name );
	    _input_variables.emplace( name );
	}
	d.setServiceTime( new LQX::VariableExpression( name, false ) );
	return name;
    }

    std::string
    JMVA_Document::setMultiplicity( const std::string& stationName, const std::string& className )
    {
	BCMP::Model::Station& m = stations().at(stationName);
	std::string name;
	/* Get a variable... $S1,$S2,... */
	std::map<const BCMP::Model::Station *,std::string>::iterator var = _multiplicity_vars.find( &m );
	if ( var != _multiplicity_vars.end() ) {
	    name = var->second;
	} else {
	    name = "$M" + std::to_string(_multiplicity_vars.size() + 1);
	    _multiplicity_vars.emplace( &m, name );
	    _input_variables.emplace( name );
	}
	m.setCopies( new LQX::VariableExpression( name, false ) );
	return name;
    }

    /*
     * A little more complicated as there are two (or more?) classes.
     * The station is ignored.  Only two classes are allowed.  The
     * Beta parameter determines the fraction of customers in
     * className, starting with 1 up to className.customers-1.
     */

    std::string
    JMVA_Document::setPopulationMix( const std::string& stationName, const std::string& className )
    {
 	if ( chains().size() != 2 ) throw std::runtime_error( "JMVA_Document::setPopulationMix" );

	const BCMP::Model::Chain::map_t::iterator i = chains().begin();
	const BCMP::Model::Chain::map_t::iterator j = std::next(i);

	/*
	 * Two new variables are needed, n1, for class 1, which is $N
	 * times class 1 customers, and n2, which is (1-$N) times
	 * class 2 customers.  Both need to be rounded to the next
	 * highest integer.  Return the "beta" value to the caller.
	 */

	const BCMP::Model::Chain::map_t::iterator k1 = i->first == className ? i : j;
	const BCMP::Model::Chain::map_t::iterator k2 = i->first == className ? j : i;
	const double k1_customers = getDoubleValue(k1->second.customers());	/* Get original (constant) values	*/
	const double k2_customers = getDoubleValue(k2->second.customers());	/* Get original (constant) values	*/

	_N1.setName( k1->first );
	_N1.setPopulation( k1_customers );
	_N2.setName( k2->first );
	_N2.setPopulation( k2_customers );
	
	const bool is_external = false;
	const std::string class1_population = "N_" + k1->first;
	const std::string x_name = "_N_" + k1->first;
	const std::string beta = "Beta";					/* Local variable	*/
	LQX::SyntaxTreeNode * assignment_expr;
	
	_population_vars.emplace( &k1->second, class1_population );
	_input_variables.emplace( class1_population );
	k1->second.setCustomers( new LQX::VariableExpression( class1_population, is_external ) );	/* ... so swap constant for variable in class.	*/
	_program.push_back( new LQX::AssignmentStatementNode( new LQX::VariableExpression( x_name, false ),
							      new LQX::ConstantValueExpression( k1_customers ) ) );
	std::vector<LQX::SyntaxTreeNode *> * function_args = new std::vector<LQX::SyntaxTreeNode *>;
	function_args->push_back( new LQX::MathExpression( LQX::MathExpression::MULTIPLY,
							   new LQX::VariableExpression( beta, false ),
							   new LQX::VariableExpression( x_name, false ) ) );
	assignment_expr = new LQX::AssignmentStatementNode( new LQX::VariableExpression( class1_population, is_external ), new LQX::MethodInvocationExpression( "round", function_args ) );
	_whatif_body.push_back( assignment_expr );

	const std::string class2_population = "N_" + k2->first;
	const std::string y_name = "_N_" + k2->first;

	_population_vars.emplace( &k2->second, class2_population );
	_input_variables.emplace( class2_population );
	k2->second.setCustomers( new LQX::VariableExpression( class2_population, is_external ) );	/* ... so swap constanst for variable in class.	*/
	_program.push_back( new LQX::AssignmentStatementNode( new LQX::VariableExpression( y_name, false ),
							      new LQX::ConstantValueExpression( k2_customers ) ) );
	function_args = new std::vector<LQX::SyntaxTreeNode *>;
	function_args->push_back( new LQX::MathExpression( LQX::MathExpression::MULTIPLY,
							   new LQX::MathExpression( LQX::MathExpression::SUBTRACT,  new LQX::ConstantValueExpression( 1. ), new LQX::VariableExpression( beta, false ) ),
							   new LQX::VariableExpression( y_name, false ) ) );
	assignment_expr = new LQX::AssignmentStatementNode( new LQX::VariableExpression( class2_population, is_external ), new LQX::MethodInvocationExpression( "round", function_args ) );
	_whatif_body.push_back( assignment_expr );
	return beta;
    }


     /*
      * Set the values for the x and x2 axes.
      */

    void
    JMVA_Document::setPopulationMixN1N2( const std::string& className, const Comprehension& population )
    {
	_N1.reserve( population.size() );
	_N2.reserve( population.size() );
	for ( size_t i = 0; i < population.size(); ++i ) {
	    const double beta = population.begin() + i * population.step();
	    _N1.emplace_back( std::pair<double,double>( beta, std::round( _N1.population() * beta ) ) );
	    _N2.emplace_back( std::pair<double,double>( beta, std::round( _N2.population() * (1.0 - beta) ) ) );
	}
    }


    /*
     * Define default result variables.
     */
     
    void
    JMVA_Document::defineDefaultResultVariables()
    {
	/* For all stations... create name_X, name_Q, name_R and name_U */
	std::for_each( stations().begin(), stations().end(), create_result( *this ) );
	std::for_each( chains().begin(), chains().end(), create_result( *this ) );
    }


    
    /*
     * Create result and observation.  Used if no result variables are present, so it sets it up as a table by station.
     */

    void
    JMVA_Document::create_result::operator()( const std::pair<const std::string,const BCMP::Model::Station>& m ) const
    {
	const BCMP::Model::Station& station = stations().at(m.first);		// Can't use m.second as m is a copy.
	for ( std::map<const std::string,const BCMP::Model::Result::Type>::const_iterator r = JMVA_Document::__result_name_table.begin(); r != JMVA_Document::__result_name_table.end(); ++r ) {
	    if ( r->second == BCMP::Model::Result::Type::RESPONSE_TIME ) continue;

	    std::string station_result = r->first + "_" + m.first;
	    std::replace( station_result.begin(), station_result.end(), ' ', '_' );		/* Remove spaces from names */

	    /* for each class.. */
	    const BCMP::Model::Station::Class::map_t& classes = station.classes();
	    if ( classes.size() > 1 ) {
		for ( BCMP::Model::Station::Class::map_t::const_iterator ki = classes.begin(); ki != classes.end(); ++ki ) {
		    const std::string class_result = station_result + "(" + ki->first + ")";
		    createObservation( m.first, class_result, r->second, &station, &ki->second );	/* Class results */
		}
	    }

	    /* total */
	    createObservation( m.first, station_result, r->second, &station );				/* Station results */
	}
    }

    void
    JMVA_Document::create_result::operator()( const std::pair<const std::string,const BCMP::Model::Chain>& k ) const
    {
	const std::string& chain_name = k.first;
	for ( std::map<const std::string,const BCMP::Model::Result::Type>::const_iterator r = JMVA_Document::__result_name_table.begin(); r != JMVA_Document::__result_name_table.end(); ++r ) {
	    if ( r->second != BCMP::Model::Result::Type::RESPONSE_TIME && r->second != BCMP::Model::Result::Type::THROUGHPUT ) continue;
	    _self.createObservation( r->first + "_" + chain_name, r->second, k.first );
	}
    }


    void
    JMVA_Document::create_result::createObservation( const std::string& station_name, const std::string& result_name, BCMP::Model::Result::Type type, const BCMP::Model::Station * m, const BCMP::Model::Station::Class * k ) const
    {
	_self.createObservation( result_name, type, m, k );
	_self.setResultIndex( result_name, station_name );
    }


    /*
     * Results for stations
     */

    LQX::SyntaxTreeNode *
    JMVA_Document::createObservation( const std::string& name, BCMP::Model::Result::Type type, const BCMP::Model::Station * m, const BCMP::Model::Station::Class * k )
    {
	/* Will need station/class names -- search map for m */
	BCMP::Model::Station::map_t::const_iterator mi = model().findStation(m);
	if ( mi == model().stations().end() ) return nullptr;

	/* Get the station object */
	LQX::MethodInvocationExpression * object = new LQX::MethodInvocationExpression( m->getTypeName(), new LQX::ConstantValueExpression( mi->first ), nullptr );

	if ( k == nullptr ) {
	    /* No class, so return station function to extract result */
	    if ( m->resultVariables().find( type ) != m->resultVariables().end() ) return object;	/* reuse existing */
	    const_cast<BCMP::Model::Station *>(m)->insertResultVariable( type, name );

	} else {
	    /* Get the class object for m the station */
	    BCMP::Model::Station::Class::map_t::const_iterator mk = m->findClass(k);
	    if ( mk == m->classes().end() ) return nullptr;
	    object = new LQX::MethodInvocationExpression( k->getTypeName(), object, new LQX::ConstantValueExpression( mk->first ), 0 );
	    if ( k->resultVariables().find( type ) != k->resultVariables().end() ) return object;	/* reuse existing */
	    const_cast<BCMP::Model::Station::Class *>(k)->insertResultVariable( type, name );

	}
	appendResultVariable( name, new LQX::AssignmentStatementNode( new LQX::VariableExpression( name, false ),
								      new LQX::ObjectPropertyReadNode( object, __lqx_function_table.at(type) ) ) );
	return object;
    }

    /*
     * Results for chains.
     */

    LQX::SyntaxTreeNode *
    JMVA_Document::createObservation( const std::string& name, BCMP::Model::Result::Type type, const std::string& chain )
    {
	BCMP::Model::Chain::map_t::const_iterator k = model().chains().find(chain);
	if ( k == model().chains().end() ) return nullptr;

	/* Get the model object */
	LQX::MethodInvocationExpression * object = new LQX::MethodInvocationExpression( "chain", new LQX::ConstantValueExpression(chain), nullptr );
	if ( (k->second).resultVariables().find( type ) != (k->second).resultVariables().end() ) return object;	/* reuse existing */

	const_cast<BCMP::Model::Chain&>(k->second).insertResultVariable( type, name );

	appendResultVariable( name, new LQX::AssignmentStatementNode( new LQX::VariableExpression( name, false ),
								      new LQX::ObjectPropertyReadNode( object, __lqx_function_table.at(type) ) ) );
	return object;
    }


    /*
     * Save the index in the _result_variables where name's results start.
     */

    void
    JMVA_Document::setResultIndex( const std::string& result_name, const std::string& station_name )
    {
	_station_index.emplace( result_name, station_name );		// Where in the array is the variable?
    }


    /*
     * Returns a list of all undefined external variables as a vector of strings
     */

    std::vector<std::string>
    JMVA_Document::getUndefinedExternalVariables() const
    {
	return std::accumulate( _input_variables.begin(), _input_variables.end(), std::vector<std::string>(), notSet(*this) );
    }

    /*
     * Strip the leading $ from name as all of the result variables are assigned inside the whatIf for loop
     */

    void
    JMVA_Document::appendResultVariable( const std::string& name, LQX::SyntaxTreeNode * expression )
    {
	if ( name.empty() ) return;
	_result_variables.emplace_back( var_name_and_expr( name, expression ) );
	_result_index.emplace( name, _result_variables.size() );
    }


    /*
     * Plot throughput/response time for system (and bounds)
     */

    void
    JMVA_Document::plot( BCMP::Model::Result::Type type, const std::string& arg )
    {
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set title \"" + model().comment() + "\"" ) );
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "#set output \"" + LQIO::Filename( getInputFileName(), "svg" )() + "\"" ) );
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "#set terminal svg" ) );

	std::ostringstream plot;		// Plot command collected here.
	plot << "plot ";

	defineDefaultResultVariables();		// Create result variables for everything for plotting.
	if ( type == BCMP::Model::Result::Type::THROUGHPUT && plotPopulationMix() ) {
	    plot_throughput_vs_population_mix( plot );
	} else if ( type == BCMP::Model::Result::Type::UTILIZATION && plotPopulationMix() ) {
	    plot_utilization_vs_population_mix( plot );
	} else if ( arg.empty() ) {
	    plot_chain( plot, type );
	} else if ( chains().find( arg ) != chains().end() ) {
	    plot_class( plot, type, arg );		/* If arg is a class, plot all stations */
	} else if ( stations().find( arg ) != stations().end() ) {
	    plot_station( plot, type, arg );		/* If arg is a station, plot all classes */
	} else {
	    throw std::invalid_argument( arg );
	}

	/* Append the plot command to the program (plot has to be near the end) */
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set datafile separator \",\"" ) );		/* Use CSV. */
	_gnuplot.push_back( LQIO::GnuPlot::print_node( plot.str() ) );
    }


    /*
     * for all stations plot class arg.
     */

    std::ostream&
    JMVA_Document::plot_class( std::ostream& plot, BCMP::Model::Result::Type type, const std::string& arg )
    {
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set xlabel \"" + _independent_variables.at(0) + "\"" ) );	// X axis
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set ylabel \"" + __y_label_table.at(type) + "\"" ) );		// Y1 axis
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set key title \"Class " + arg + "\"" ) );
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set key top left box" ) );

	const size_t x = 1;		/* GNUPLOT starts from 1, not 0 */
	size_t count = 0;
	for ( BCMP::Model::Station::map_t::const_iterator m = stations().begin(); m != stations().end(); ++m ) {
	    if ( m->second.reference() || !m->second.hasClass(arg) ) continue;
	    const BCMP::Model::Station::Class::map_t& classes = m->second.classes();
	    const BCMP::Model::Station::Class& k = classes.at(arg);

	    std::string name;
	    if ( classes.size() == 1 ) {
		name = m->second.resultVariables().at(type);
	    } else {
		name = k.resultVariables().at(type);
	    }

	    /* Append plot command to plot */
	    if ( count > 0 ) plot << ", ";
	    plot << "\"$DATA\" using " << x << ":" << get_gnuplot_index(name) << " with linespoints" << " title \"" << m->first << "\"";

	    ++count;
	}

	return plot;
    }

    /*
     * for station arg plot all classes
     */

    std::ostream&
    JMVA_Document::plot_station( std::ostream& plot, BCMP::Model::Result::Type type, const std::string& name )
    {
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set xlabel \"" + _independent_variables.at(0) + "\"" ) );	// X axis
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set ylabel \"" + __y_label_table.at(type) + "\"" ) );		// Y1 axis
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set key title \"Station " + name + "\"" ) );
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set key top left box" ) );

	const size_t x = 1;		/* GNUPLOT starts from 1, not 0 */

	const BCMP::Model::Station& station = stations().at( name );
	if ( station.classes().size() == 1 ) {
	    plot << "\"$DATA\" using " << x << ":" << get_gnuplot_index(station.resultVariables().at(type)) << " with linespoints";
	} else {
	    for ( BCMP::Model::Station::Class::map_t::const_iterator k = station.classes().begin(); k != station.classes().end(); ++k ) {
		if ( k != station.classes().begin() ) plot << ", ";
		plot << "\"$DATA\" using " << x << ":" << get_gnuplot_index(k->second.resultVariables().at(type)) << " with linespoints" << " title \"" << k->first << "\"";

	    }
	}

	return plot;
    }


    std::ostream&
    JMVA_Document::plot_chain( std::ostream& plot, BCMP::Model::Result::Type type )
    {
	if ( type != BCMP::Model::Result::Type::THROUGHPUT && type != BCMP::Model::Result::Type::RESPONSE_TIME ) return plot;		// makes no sense otherwise.

	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set xlabel \"" + _independent_variables.at(0) + "\"" ) );	// X axis
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set ylabel \"" + __y_label_table.at(type) + "\"" ) );		// Y1 axis
	if ( type == BCMP::Model::Result::Type::THROUGHPUT ) {
	    _gnuplot.push_back( LQIO::GnuPlot::print_node( "set key bottom right" ) );
	} else {
	    _gnuplot.push_back( LQIO::GnuPlot::print_node( "set key top left" ) );
	}
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set key box" ) );

	std::deque<Comprehension>::const_iterator whatif = std::find_if( this->whatif_statements().begin(), this->whatif_statements().end(), Comprehension::find(_independent_variables.at(0)) );
	if ( whatif == whatif_statements().end() || whatif->size() <= 1 ) throw std::invalid_argument( _independent_variables.at(0) + " is not a whatif" );

	LQX::ConstantValueExpression * x_max = new LQX::ConstantValueExpression( whatif->max() );

	size_t n_labels = 0;
	LQX::SyntaxTreeNode * y_max = nullptr;

	const size_t x = 1;		/* GNUPLOT starts from 1, not 0 */

	for ( BCMP::Model::Chain::map_t::const_iterator k = chains().begin(); k != chains().end(); ++k ) {
	    BCMP::Model::Bound bounds( *k, stations() );
	    if ( k != chains().begin() ) plot << ", ";

	    /* Append plot command to plot */
	    std::string title;
	    if ( chains().size() > 1 ) {
		title = k->first + " ";
	    }
	    title += "MVA";
	    plot << "\"$DATA\" using " << x << ":" << get_gnuplot_index(k->second.resultVariables().at(type)) << " with linespoints" << " title \"" << title << "\"";

	    /* Now plot the bounds. */
	    std::ostringstream label1;
	    std::ostringstream label2;
	    std::string title1;
	    std::string title2;
	    LQX::SyntaxTreeNode * nStar = bounds.N_star();
	    LQX::SyntaxTreeNode * bound1 = nullptr;

	    if ( chains().size() > 1 ) {
		title1 = k->first + " ";
		title2 = k->first + " ";
	    }
	    switch ( type ) {
	    case BCMP::Model::Result::Type::THROUGHPUT:
		bound1 = BCMP::Model::reciprocal( bounds.D_max() );
		title1 += "1/Dmax";
		title2 += "1/(Dsum+Z)";
		break;
	    case BCMP::Model::Result::Type::RESPONSE_TIME:
		bound1 = bounds.D_sum();
		title1 += "Dsum";
		title2 += "N*Dmax-Z";
		break;
	    default:
		break;
	    }

	    n_labels += 1;
	    label1 << "set label " << n_labels << " \"" << *bound1 << "\" at " << 0.2 << "," << *bound1 << " * 1.02," << 0. << " left";
	    _gnuplot.push_back( LQIO::GnuPlot::print_node( label1.str() ) );

	    n_labels += 1;
	    label2 << "set label " << n_labels << " \"N*=" << *nStar << "\" at " << *nStar << "," << *bound1 << "* 1.02," << 0. << " right";
	    _gnuplot.push_back( LQIO::GnuPlot::print_node( label2.str() ) );

	    plot << ", " << *bound1 << " with lines title \"" << title1 << "\"";
	    switch ( type ) {
	    case BCMP::Model::Result::Type::THROUGHPUT:
		plot << ", x/" << *BCMP::Model::add( bounds.D_sum(), bounds.Z_sum() ) << " with lines title \"" << title2 << "\"";
		y_max = BCMP::Model::max( y_max, BCMP::Model::reciprocal( bounds.D_max() ) );
		break;
	    case BCMP::Model::Result::Type::RESPONSE_TIME:
		plot << ", x*" << *bounds.D_max() <<  "-" << *bounds.Z_sum() << " with lines title \"" << title2 << "\"";
		y_max = BCMP::Model::max( y_max, BCMP::Model::subtract( BCMP::Model::multiply( x_max, bounds.D_max() ), bounds.Z_sum() ) );
		break;
	    default:
		break;
	    }
	}
	std::ostringstream yrange;
	yrange << "set yrange [" << 0 << ":" << *y_max << " * 1.10]";
	_gnuplot.push_back( LQIO::GnuPlot::print_node( yrange.str() ) );
	return plot;
    }

    /*
     * Plot the results of a population mix.  X-axis is class 1,
     * Y-axis is class 2.  I should possibly label a few points, but
     * they might have to computed by gnuplot.
     */

    std::ostream&
    JMVA_Document::plot_throughput_vs_population_mix( std::ostream& plot )
    {
	const BCMP::Model::Chain::map_t::const_iterator x = chains().find( _N1.name() );	// User may have picked class 2 first...
	const BCMP::Model::Chain::map_t::const_iterator y = chains().find( _N2.name() );

	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set xlabel \"" + x->first + " Throughput\"" ) );	// X axis
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set ylabel \"" + y->first + " Throughput\"" ) );	// Y1 axis
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set key bottom left box" ) );

	const BCMP::Model::Result::Type type = BCMP::Model::Result::Type::THROUGHPUT;
	plot << "\"$DATA\" using " << get_gnuplot_index(x->second.resultVariables().at(type)) << ":" << get_gnuplot_index(y->second.resultVariables().at(type)) <<  " with linespoints title \"MVA\"";

	/* Compute bound for each station */

	LQX::SyntaxTreeNode * x_max = nullptr;
	LQX::SyntaxTreeNode * y_max = nullptr;
	for ( BCMP::Model::Station::map_t::const_iterator m = stations().begin(); m != stations().end(); ++m ) {
	    if ( m->second.type() != BCMP::Model::Station::Type::LOAD_INDEPENDENT && m->second.type() != BCMP::Model::Station::Type::MULTISERVER ) continue;

	    LQX::SyntaxTreeNode * D_x = BCMP::Model::Bound::D( m->second, *x );		/* Adjusted for multiservers	*/
	    LQX::SyntaxTreeNode * D_y = BCMP::Model::Bound::D( m->second, *y );
	    if ( D_x == nullptr && D_y == nullptr ) continue;

	    x_max = BCMP::Model::max( x_max, D_x );
	    y_max = BCMP::Model::max( y_max, D_y );
	    plot << ",\\" << std::endl << "     ";     	/* New line for each bound */
	    if ( BCMP::Model::isDefault( D_y, 0. ) ) {
		plot << "1/" << *D_x << ",t";
	    } else {
		plot << "t,(1-t*" << *D_x << ")/" << *D_y;
	    }
	    plot << " with lines";
	    /* Line colour would go here */
	    plot << " title \"" << m->first << " Bound\"";
	}

	/* Set range (if possible), otherwise punt */
	if ( !BCMP::Model::isDefault( x_max ) && !BCMP::Model::isDefault( y_max ) ) {
	    LQX::SyntaxTreeNode * x_pos = BCMP::Model::reciprocal( x_max );
	    LQX::SyntaxTreeNode * y_pos = BCMP::Model::reciprocal( y_max );
	    _gnuplot.push_back( LQIO::GnuPlot::print_node( "set parametric" ) );
	    _gnuplot.push_back( LQIO::GnuPlot::print_node( new LQX::ConstantValueExpression( "set xrange [0:" ),
							   BCMP::Model::multiply( x_pos, new LQX::ConstantValueExpression(1.05) ),
							   new LQX::ConstantValueExpression( "]" ),
							   nullptr ) );
	    _gnuplot.push_back( LQIO::GnuPlot::print_node( new LQX::ConstantValueExpression( "set trange [0:" ),
							   BCMP::Model::multiply( x_pos, new LQX::ConstantValueExpression(1.05) ),
							   new LQX::ConstantValueExpression( "]" ),
							   nullptr ) );
	    _gnuplot.push_back( LQIO::GnuPlot::print_node( new LQX::ConstantValueExpression( "set yrange [0:" ),
							   BCMP::Model::multiply( y_pos, new LQX::ConstantValueExpression(1.05) ),
							   new LQX::ConstantValueExpression( "]" ),
							   nullptr ) );

	    std::ostringstream label_1, label_2;
	    label_1 << ")\" at "   << getDoubleValue(x_pos) * 0.01 << "," << getDoubleValue(y_pos)         << " left";
	    label_2 << ",0)\" at " << getDoubleValue(x_pos)        << "," << getDoubleValue(y_pos) * 0.03  << " right";

	    _gnuplot.push_back( LQIO::GnuPlot::print_node( new LQX::ConstantValueExpression("set label \"(0,"), y_pos, new LQX::ConstantValueExpression(label_1.str()), nullptr ) );
	    _gnuplot.push_back( LQIO::GnuPlot::print_node( new LQX::ConstantValueExpression("set label \"("),   x_pos, new LQX::ConstantValueExpression(label_2.str()), nullptr ) );
	}

	return plot;
    }


    /*
     * Plot the utilization versus the population mix.
     */

    std::ostream&
    JMVA_Document::plot_utilization_vs_population_mix( std::ostream& plot )
    {
	const BCMP::Model::Result::Type type = BCMP::Model::Result::Type::UTILIZATION;

	/* Generate tics for x axes */
	std::ostringstream xtics;
	for ( size_t i = 0; i < _N1.size(); ++i ) {
	    if ( i != 0 ) {
		xtics << ", ";
	    }
	    xtics << "\"(" <<  _N1[i].second << "," << _N2[i].second << ")\" " << _N1[i].first;
	}
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set xtics (" + xtics.str() + ")" ) );
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set xlabel \" Customers (" + _N1.name() + "," + _N2.name() + ")\"" ) );
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set ylabel \""  + __y_label_table.at(type) + "\"" ) );	// Y1 axis
	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set key title \"Station\" box" ) );
//	_gnuplot.push_back( LQIO::GnuPlot::print_node( "set key top left" ) );

	/* Find utilization for all stations */

	size_t count = 0;
	for ( BCMP::Model::Station::map_t::const_iterator m = stations().begin(); m != stations().end(); ++m ) {
	    if ( m->second.type() != BCMP::Model::Station::Type::LOAD_INDEPENDENT && m->second.type() != BCMP::Model::Station::Type::MULTISERVER ) continue;
	    if ( count > 0 ) plot << ", \\" << std::endl << "     ";
	    plot << "\"$DATA\" using 1:" << get_gnuplot_index(m->second.resultVariables().at(type)) <<  " with linespoints title \"" << m->first << "\"";
	    ++count;
	}
	plot << std::endl;

#if UTILIZATION_BOUNDS
	std::map<Intercepts::point,std::vector<double>> bounds;
	Intercepts( *this, _N1.name(), _N2.name() ).compute( bounds );
	double max_x = bounds.rbegin()->first.x();	// Find the largest value of x.
	
	/* Plot the bounds */

	plot << "$BOUNDS << EOF" << std::endl;
	/* Ouptut bounds */
	for ( const auto& bound : bounds ) {
	    plot << bound.first.x() / max_x;		// Normalize to 0:1 (like population mix Beta
	    for ( std::vector<double>::const_iterator i = bound.second.begin(); i != bound.second.end(); ++i ) {
		plot << "," << *i;
	    }
	    plot << std::endl;
	}
	plot << "EOF" << std::endl;

	size_t index = 2;
	plot << "plot ";
	for ( BCMP::Model::Station::map_t::const_iterator m = stations().begin(); m != stations().end(); ++m ) {
	    if ( m->second.type() != BCMP::Model::Station::Type::LOAD_INDEPENDENT && m->second.type() != BCMP::Model::Station::Type::MULTISERVER ) continue;
	    if ( index > 2 ) plot << ", \\" << std::endl << "     ";
	    plot << "\"$BOUNDS\" using 1:" << index <<  " with lines dt 2 title \"" << m->first << " bound\"";
	    index += 1;
	}
	plot << std::endl;
#endif

	return plot;
    }


    /*
     * Find the result in the data file for name.
     * The independent variables are printed first, followed by the results.
     */

    size_t
    JMVA_Document::get_gnuplot_index( const std::string& name ) const
    {
	return _result_index.at( name );
    }


    const JMVA_Document::Intercepts&
    JMVA_Document::Intercepts::compute( std::map<point,std::vector<double> >& results ) const
    {
	const BCMP::Model::Chain::map_t::const_iterator x = chains().find( _chain_1 );	// User may have picked class 2 first...
	const BCMP::Model::Chain::map_t::const_iterator y = chains().find( _chain_2 );

	/* Compute intercepts */

	std::vector<point> D_xy;
	double Dmax_x = 0.0;
	double Dmax_y = 0.0;
	for ( BCMP::Model::Station::map_t::const_iterator m = stations().begin(); m != stations().end(); ++m ) {
	    if ( m->second.type() != BCMP::Model::Station::Type::LOAD_INDEPENDENT && m->second.type() != BCMP::Model::Station::Type::MULTISERVER ) continue;

	    /* At this pint in the game, the values are resolved and we don't need results */
	    const double D_x = getDoubleValue( BCMP::Model::Bound::D( m->second, *x ) );		/* Adjusted for multiservers	*/
	    const double D_y = getDoubleValue( BCMP::Model::Bound::D( m->second, *y ) );
	    if ( D_x == 0 && D_y == 0 ) continue;

	    Dmax_x = std::max( D_x, Dmax_x );
	    Dmax_y = std::max( D_y, Dmax_y );
	    D_xy.emplace_back( point( 1./std::max(D_x,1e-20), 1./std::max(D_y,1e-20) ) );		/* truncate at 1e20.		*/
	}
	std::vector<point> intercepts;
	for ( std::vector<point>::const_iterator l1 = D_xy.begin(); l1 != D_xy.end(); ++l1 ) {
	    for ( std::vector<point>::const_iterator l2 = std::next(l1); l2 != D_xy.end(); ++l2 ) {
		intercepts.emplace_back( compute( point( 0, l1->y() ), point( l1->x(), 0 ), point( 0, l2->y() ), point( l2->x(), 0 ) ) );
	    }
	}

	/* Compute utilization at all stations at all intercepts.  Reject all with utlization > 1.  Sort on x */

	const double tput_x = 1 / Dmax_x;
	const double tput_y = 1 / Dmax_y;
	size_t index = 0;
	for ( BCMP::Model::Station::map_t::const_iterator m = stations().begin(); m != stations().end(); ++m, ++index ) {
	    const BCMP::Model::Station& station = m->second;
	    if ( station.type() != BCMP::Model::Station::Type::LOAD_INDEPENDENT && station.type() != BCMP::Model::Station::Type::MULTISERVER ) continue;

	    add_result( results, point( 0, tput_y ), index, getDoubleValue( station.demand( station.classAt( y->first ) ) ) * tput_y );
	    for ( std::vector<point>::const_iterator i = intercepts.begin(); i != intercepts.end(); ++i ) {
		if ( i->x() < 0 || tput_x < i->x() ) continue;	/* Infeasible, discard		*/
		if ( i->y() < 0 || tput_x < i->y() ) continue;	/* Infeasible, discard		*/
		const double U_x = getDoubleValue( station.demand( station.classAt( x->first ) ) ) * i->x();
		const double U_y = getDoubleValue( station.demand( station.classAt( y->first ) ) ) * i->y();
		std::cerr << "(" << i->x() << "," << i->y() << ")" << " U_x = " << U_x << ", U_y = " << U_y << ", U = " << U_x + U_y << std::endl;
	        add_result( results, *i, index, std::min( U_x + U_y, 1.0 ) );
	    }
	    add_result( results, point( tput_x, 0 ), index, getDoubleValue( station.demand( station.classAt( x->first ) ) ) * tput_x );
	}
	return *this;
    }

    /*
     * Compute the intercepts of the line defined by p1 and p2 with the line defined by p3 and p4.
     */
    
    JMVA_Document::Intercepts::point
    JMVA_Document::Intercepts::compute( const point& p1, const point& p2, const point& p3, const point& p4 ) const
    {
	const double denominator = (p1.x() - p2.x()) * (p3.y() - p4.y()) - (p1.y() - p2.y()) * (p3.x() - p4.x());
	if ( denominator != 0 ) {
	    const double numerator1 = p1.x() * p2.y() - p1.y() * p2.x();
	    const double numerator2 = p3.x() * p4.y() - p3.y() * p4.x();
	    return point( ((numerator1 * (p3.x() - p4.x())) - ((p1.x() - p2.x()) * numerator2)) / denominator,
		          ((numerator1 * (p3.y() - p4.y())) - ((p1.y() - p2.y()) * numerator2)) / denominator );
	} else {
	    return point( 0., 0. );
	}
    }


    void
    JMVA_Document::Intercepts::add_result( std::map<point,std::vector<double> >& results, const point& intercept, size_t index, double utilization ) const
    {
	const std::pair<std::map<point,std::vector<double> >::iterator,bool> result = results.emplace( intercept, std::vector<double>() );
	std::vector<double>& value = result.first->second;
	if ( result.second ) {
	    value.resize(stations().size(),0.0);	// created a new item.  Allocate and clear storage.
	}
	value.at(index) = utilization;			// Plotting versus x.
    }

    std::ostream& JMVA_Document::Intercepts::point::print( std::ostream& output ) const 
    {
	output << "(" << x() << "," << y() << ")";
	return output;
    }
}

namespace QNIO {

    /* ---------------------------------------------------------------- */
    /* Output.								*/
    /* ---------------------------------------------------------------- */

    std::ostream&
    JMVA_Document::print( std::ostream& output ) const
    {
	XML::set_indent(0);
	output << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>" << std::endl
	       << "<!-- " << LQIO::DOM::Common_IO::svn_id() << " -->" << std::endl;
	if ( LQIO::io_vars.lq_command_line.size() > 0 ) {
	    output << "<!-- " << LQIO::io_vars.lq_command_line << " -->" << std::endl;
	}

	printModel( output );
	printResults( output );
	output << XML::end_element( Xmodel ) << std::endl;
	return output;
    }

    std::ostream&
    JMVA_Document::exportModel( std::ostream& output, bool bounds ) const
    {
	XML::set_indent(0);
	output << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>" << std::endl
	       << "<!-- " << LQIO::DOM::Common_IO::svn_id() << " -->" << std::endl;
	if ( LQIO::io_vars.lq_command_line.size() > 0 ) {
	    output << "<!-- " << LQIO::io_vars.lq_command_line << " -->" << std::endl;
	}

	printModel( output, bounds );
	if ( !strictJMVA() ) {
	    printSPEX( output );
	} else if ( !bounds ) {
	    printResults( output );
	}
	output << XML::end_element( Xmodel ) << std::endl;
	return output;
    }

    std::ostream&
    JMVA_Document::printModel( std::ostream& output, bool bounds ) const
    {
	std::for_each( stations().begin(), stations().end(), BCMP::Model::pad_demand( chains() ) );	/* JMVA want's zeros */

	if ( hasPragmas() ) {
	    const std::map<std::string,std::string>& pragmas = getPragmaList();
	    for ( std::map<std::string,std::string>::const_iterator next_pragma = pragmas.begin(); next_pragma != pragmas.end(); ++next_pragma ) {
		output << XML::start_element( Xpragma, false )
		       << XML::attribute( Xparam, next_pragma->first )
		       << XML::attribute( Xvalue, next_pragma->second )
		       << XML::end_element( Xpragma, false ) << std::endl;
	    }
	}
	output << XML::start_element( Xmodel );
	if ( bounds ) {
	    output << XML::attribute( Xjaba, Xtrue );
	}
	output << XML::attribute( "xmlns:xsi", std::string("http://www.w3.org/2001/XMLSchema-instance") )
	       << XML::attribute( "xsi:noNamespaceSchemaLocation", std::string("JMTmodel.xsd") )
	       << ">" << std::endl;

	if ( !model().comment().empty() ) {
	    output << XML::start_element( Xdescription ) << ">" << std::endl
		   << XML::cdata( model().comment() ) << std::endl
		   << XML::end_element( Xdescription ) << std::endl;
	}

	output << XML::start_element( Xparameters ) << ">" << std::endl;

	output << XML::start_element( Xclasses ) << XML::attribute( Xnumber, static_cast<unsigned int>(chains().size()) ) << ">" << std::endl;
	std::for_each( chains().begin(), chains().end(), printClass( output, model(), strictJMVA() ) );
	output << XML::end_element( Xclasses ) << std::endl;

	output << XML::start_element( Xstations ) << XML::attribute( Xnumber, static_cast<unsigned int>(stations().size()) ) << ">" << std::endl;
	std::for_each( stations().begin(), stations().end(), printStation( output, model(), strictJMVA() ) );
	output << XML::end_element( Xstations ) << std::endl;

	if ( !bounds ) {
	    output << XML::start_element( XReferenceStation ) << XML::attribute( Xnumber, static_cast<unsigned int>(chains().size()) ) << ">" << std::endl;
	    std::for_each( chains().begin(), chains().end(), printReference( output, stations() ) );
	    output << XML::end_element( XReferenceStation ) << std::endl;

	    output << XML::start_element( XalgParams ) << ">" << std::endl
		   << XML::simple_element( XalgType ) << XML::attribute( "maxSamples", 10000U ) << XML::attribute( Xname, std::string("MVA") ) << XML::attribute( "tolerance", 1.0E-7 ) << XML::end_element( XalgType, false ) << std::endl
		   << XML::simple_element( XcompareAlgs ) << XML::attribute( Xvalue, false ) << XML::end_element( XcompareAlgs, false )  << std::endl
		   << XML::end_element( XalgParams ) << std::endl;

	    /* SPEX */
	    /* Insert WhatIf for statements for arrays and completions. */
	    /* 	<whatIf className="c1" stationName="p2" type="Service Demands" values="1.0;1.1;1.2;1.3;1.4;1.5;1.6;1.7;1.8;1.9;2.0"/> */

	    if ( !_input_variables.empty() ) {
		output << "   <!-- SPEX input variables -->" << std::endl;
		What_If what_if( output, *this );
		std::for_each( _input_variables.begin(),  _input_variables.end(),  what_if );		/* Do arrays in order	*/
	    }
	}

	return output;
    }

    /*
     * Output all results in the canonical JMVA format.
     */
     
    std::ostream&
    JMVA_Document::printResults( std::ostream& output ) const
    {
	const static std::map<const BCMP::Model::Result::Type,const char *> measure_type = {
	    { BCMP::Model::Result::Type::QUEUE_LENGTH,	  XNumber_of_Customers },
	    { BCMP::Model::Result::Type::RESIDENCE_TIME,  XResidence_Time },
	    { BCMP::Model::Result::Type::RESPONSE_TIME,   XResponse_Time },
	    { BCMP::Model::Result::Type::THROUGHPUT,      XThroughput },
	    { BCMP::Model::Result::Type::UTILIZATION,     XUtilization }
	};
	output << "   <!-- " << LQIO::io_vars.lq_toolname << " results -->" << std::endl;
	unsigned int count = 0;
	for ( const auto& i : _results ) {
	    output << XML::start_element( Xsolutions )
		   << XML::attribute( Xiteration, count )
		   << XML::attribute( XiterationValue, static_cast<unsigned int>(i.first) )
		   << XML::attribute( Xok, Xtrue )
		   << ">" << std::endl;
	    output << XML::start_element( Xalgorithm )
		   << XML::attribute( Xname, _mva_info.at(i.first).first )		/* Alg. name */
		   << XML::attribute( Xiterations, static_cast<unsigned int>(_mva_info.at(i.first).second) )	/* Iterations */
		   << ">" << std::endl;
	    for ( const auto& m : i.second ) {
		output << XML::start_element( Xstationresults ) << XML::attribute( Xstation, m.first ) << ">" << std::endl;
		for ( const auto& k: m.second ) {
		    output << XML::start_element( Xclassresults ) << XML::attribute( Xcustomerclass, k.first ) << ">" << std::endl;
		    for ( const auto& r : k.second ) {
			output << XML::start_element( Xmeasure, false )
			       << XML::attribute( XmeasureType, measure_type.at(r.first) )
			       << XML::attribute( XmeanValue, r.second )
			       << "/>" << std::endl;
		    }
		    output << XML::end_element( Xclassresults ) << std::endl;
		}
		output << XML::end_element( Xstationresults ) << std::endl;
	    }
	    output << XML::end_element( Xalgorithm ) << std::endl;
	    output << XML::end_element( Xsolutions ) << std::endl;
	    count += 1;
	}
	return output;
    }

    /* SPEX */
    std::ostream&
    JMVA_Document::printSPEX( std::ostream& output ) const
    {
	/* Insert a results section, but only to output the variables */

	if ( !_result_variables.empty() ) {
	    output << "   <!-- SPEX results -->" << std::endl;
	    /* Store in a map<station,pair<string,map<class,string>>, then output by station and class. */
	    output << XML::start_element( Xsolutions ) << XML::attribute( Xok, Xfalse ) << ">" << std::endl;
	    output << XML::start_element( Xalgorithm ) << XML::attribute( Xiterations, static_cast<unsigned int>(0) ) << ">" << std::endl;
	    /* Output by station, then class */
	    for ( BCMP::Model::Station::map_t::const_iterator m = stations().begin(); m != stations().end(); ++m ) {
		output << XML::start_element( Xstationresults ) << XML::attribute( Xstation, m->first ) << ">" << std::endl;
		const BCMP::Model::Station& station = m->second;
		const BCMP::Model::Result::map_t& station_variables = station.resultVariables();
		std::for_each( station_variables.begin(), station_variables.end(), printMeasure( output ) );
		for ( BCMP::Model::Station::Class::map_t::const_iterator k = station.classes().begin(); k != station.classes().end(); ++k ) {
		    output << XML::start_element( Xclassresults ) << XML::attribute( Xcustomerclass, k->first ) << ">" << std::endl;
		    const BCMP::Model::Station::Class& clasx = k->second;
		    const BCMP::Model::Result::map_t& class_variables = clasx.resultVariables();
		    std::for_each( class_variables.begin(), class_variables.end(), printMeasure( output ) );
		    output << XML::end_element( Xclassresults ) << std::endl;
		}
		output << XML::end_element( Xstationresults ) << std::endl;
	    }
	    output << XML::end_element( Xalgorithm ) << std::endl;
	    output << XML::end_element( Xsolutions ) << std::endl;
	}
	return output;
    }

    double
    JMVA_Document::printCommon::getDoubleValue( LQX::SyntaxTreeNode * value ) const
    {
	return value->invoke( _model.environment() )->getDoubleValue();
    }


    /*
     * Print out a comment if v is not a constant value expression.
     */
    
    std::ostream&
    JMVA_Document::printCommon::print_comment( std::ostream& output, LQX::SyntaxTreeNode* v ) const
    {
	if ( strictJMVA() && v != nullptr && dynamic_cast<LQX::ConstantValueExpression *>(v) == nullptr ) {
	    output << "    " << "<!--" << *v << "-->";
	}
	return output;
    }


    /*
     * Output either the value of the attribute, or it's expression.  Null attributes are zero.
     */
    
    std::ostream&
    JMVA_Document::printCommon::print_attribute( std::ostream& output, const std::string& a, LQX::SyntaxTreeNode* v ) const
    {
	if ( strictJMVA() || v == nullptr ) output << XML::attribute( a, static_cast<unsigned int>( getDoubleValue(v) ) );
	else output << XML::attribute( a, *v );
	return output;
    }


    /*+ BUG_411
     * Print the station. servers=<n> is not implemented in JMVA (and
     * causes issues in n !=1), but we use it for multiservers (and
     * don't use the lists)
     */

    void
    JMVA_Document::printStation::operator()( const BCMP::Model::Station::pair_t& m ) const
    {
	const BCMP::Model::Station& station = m.second;
	static const std::map<BCMP::Model::Station::Type,const char * const> type = {
	    { BCMP::Model::Station::Type::DELAY, Xdelaystation },
	    { BCMP::Model::Station::Type::MULTISERVER, Xldstation },
	    { BCMP::Model::Station::Type::LOAD_INDEPENDENT, Xlistation }
	};
	const char * const element = type.at(station.type());

	_output << XML::start_element( element ) << XML::attribute( Xname, m.first );
	_output << ">" << std::endl;
	_output << XML::start_element( Xservicetimes ) << ">" << std::endl;
	if ( station.type() == BCMP::Model::Station::Type::MULTISERVER ) {
	    if ( !strictJMVA() && station.copies() != nullptr ) _output << XML::attribute( Xservers, *station.copies() );	// BUG_411 strict JMVA! copies==1.
	    std::for_each( station.classes().begin(), station.classes().end(), printServiceTimeList( _output, _model, station.copies() ) );		// strict jmva -- need total customers.  Won't work for mixed networks.
	} else {
	    std::for_each( station.classes().begin(), station.classes().end(), printServiceTime( _output, _model, strictJMVA() ) );
	}
	_output << XML::end_element( Xservicetimes ) << std::endl;
	_output << XML::start_element( Xvisits ) << ">" << std::endl;
	std::for_each( station.classes().begin(), station.classes().end(), printVisits( _output, _model, strictJMVA() ) );
	_output << XML::end_element( Xvisits ) << std::endl;
	_output << XML::end_element( element ) << std::endl;
    }

    void
    JMVA_Document::printClass::operator()( const BCMP::Model::Chain::pair_t& k ) const
    {
	if ( k.second.isClosed() ) {
	    _output << XML::simple_element( Xclosedclass )
		    << XML::attribute( Xname, k.first );
	    print_attribute( _output, Xpopulation, k.second.customers() );
	    _output << XML::end_element( Xclosedclass, false );
	    print_comment( _output, k.second.customers() );
	    _output << std::endl;
	} else if ( k.second.isOpen() ) {
	    _output << XML::simple_element( Xopenclass )
		    << XML::attribute( Xname, k.first );
	    print_attribute( _output, Xrate, k.second.arrival_rate() );
	    _output << XML::end_element( Xopenclass );
	    print_comment( _output, k.second.arrival_rate() );
	    _output << std::endl;
	} else {
	    throw std::range_error( std::string( "JMVA_Document::printClass::operator(): Undefined class." ) + k.first );
	}
    }

    void
    JMVA_Document::printReference::operator()( const BCMP::Model::Chain::pair_t& k ) const
    {
	BCMP::Model::Station::map_t::const_iterator m = std::find_if( _stations.begin(), _stations.end(), &BCMP::Model::Station::isCustomer );
	if ( m != _stations.end() ) {
	    _output << XML::simple_element( XClass )
		    << XML::attribute( Xname, k.first )
		    << XML::attribute( XrefStation, m->first )
		    << XML::end_element( XClass, false ) << std::endl;
	}
    }


    void
    JMVA_Document::printServiceTime::operator()( const BCMP::Model::Station::Class::pair_t& d ) const
    {
	std::ostringstream service_time;
	if ( strictJMVA() || d.second.service_time() == nullptr ) {
	    service_time << getDoubleValue( d.second.service_time() );
	} else {
	    service_time << *d.second.service_time();
	}
	_output << XML::inline_element( Xservicetime, Xcustomerclass, d.first, service_time.str() );
	print_comment( _output, d.second.service_time() );
	_output << std::endl;
    }

    /*+
     * BUG_411
     * For LIStations (FESC).  We have a list of service times.  For
     * us, it's a multiserver.  For JMVA, the number of elements ==
     * the number of servers.  More likely, we need the total number
     * of customers, and for multiclass that may be a problem.
     */

    JMVA_Document::printServiceTimeList::printServiceTimeList( std::ostream& output, const BCMP::Model& model, LQX::SyntaxTreeNode * copies )
	: printCommon( output, model, true ), _copies(copies), _customers(nullptr)
    {
	_customers = std::accumulate( std::next(chains().begin()), chains().end(), chains().begin()->second.customers(), add_customers );
    }

    void
    JMVA_Document::printServiceTimeList::operator()( const BCMP::Model::Station::Class::pair_t& d ) const
    {
	_output << XML::start_element( Xservicetimes, false )
		<< XML::attribute( Xcustomerclass, d.first )
		<< ">";

	/* if _copies is a constant, then output a list... */
	const double copies = getDoubleValue( _copies );
	const double customers = getDoubleValue( _customers );
	const double service_time = getDoubleValue( d.second.service_time() );
	const double count = std::max( customers, copies );
	if ( count > 0 ) {
	    for ( double i = 1.; i <= count; ++i ) {
		if ( i != 1. ) _output << ";";
		_output << service_time / std::min( i, copies );	/* Pad with max service rate. */
	    }
	} else if ( d.second.service_time() != nullptr ) { // BUG_411 deprecate.
	    _output << *d.second.service_time();
	} else {
	    _output << 0;
	}
	_output << "</" << Xservicetimes << ">" << std::endl;
    }
    
    void
    JMVA_Document::printVisits::operator()( const BCMP::Model::Station::Class::pair_t& d ) const
    {
	std::ostringstream visits;
	if ( strictJMVA() || d.second.visits() == nullptr ) {
	    visits << getDoubleValue( d.second.visits() );
	} else {
	    visits << *d.second.visits();
	}
	_output << XML::inline_element( Xvisit, Xcustomerclass, d.first, visits.str() );
	print_comment( _output, d.second.visits() );
	_output << std::endl;
    }

    /*
     * Insert SPEX result variables.  The <measure> element is hijacked by putting the result variable
     * into the meanValue for the measure.
     */

    void
    JMVA_Document::printMeasure::operator()( const BCMP::Model::Result::pair_t& r ) const
    {
	static const std::map<const BCMP::Model::Result::Type,const char * const> attribute = {
	    { BCMP::Model::Result::Type::QUEUE_LENGTH, XNumberOfCustomers },
	    { BCMP::Model::Result::Type::RESIDENCE_TIME, XResidenceTime },
	    { BCMP::Model::Result::Type::THROUGHPUT, XThroughput },
	    { BCMP::Model::Result::Type::UTILIZATION, XUtilization }
	};
	_output << XML::simple_element( Xmeasure )
		<< XML::attribute( XmeasureType, attribute.at(r.first) )
		<< XML::attribute( XmeanValue, r.second )
		<< XML::end_element( Xmeasure, false ) << std::endl;
    }

    /*
     * Generate for WhatIf.
     * <whatIf className="c1" type="Customer Numbers" values="4.0;5.0;6.0;7.0;8.0"/>
     * <whatIf className="c1" stationName="p1" type="Service Demands" values="0.4;0.44000000000000006;0.4800000000000001;0.5200000000000001;0.56;0.6;0.64;0.68;0.72;0.76;0.8"/>
     */

    void
    JMVA_Document::What_If::operator()( const std::string& var ) const
    {
	/* Find the variable */
	BCMP::Model::Station::map_t::const_iterator m;
	BCMP::Model::Chain::map_t::const_iterator k;
	if ( (k = std::find_if( chains().begin(), chains().end(), What_If::has_customers( var ) )) != chains().end() ) {
	    _output << XML::simple_element( XwhatIf )
		    << XML::attribute( XclassName, k->first )
		    << XML::attribute( Xtype, XCustomer_Numbers );
	} else if ( (k = std::find_if( chains().begin(), chains().end(), What_If::has_customers( var ) )) != chains().end() ) {
	    _output << XML::simple_element( XwhatIf )
		    << XML::attribute( XclassName, k->first )
		    << XML::attribute( Xtype, XArrival_Rates );
	} else if ( (m = std::find_if( stations().begin(), stations().end(), What_If::has_copies( var ) )) != stations().end() ) {
	    _output << XML::simple_element( XwhatIf )
		    << XML::attribute( XstationName, m->first )
		    << XML::attribute( Xtype, XNumber_of_Servers );
	} else if ( (m = std::find_if( stations().begin(), stations().end(), What_If::has_var( var ) )) != stations().end() ) {
	    const BCMP::Model::Station::Class::map_t& classes = m->second.classes();
	    BCMP::Model::Station::Class::map_t::const_iterator d;
	    _output << XML::simple_element( XwhatIf );
	    if ( (d = std::find_if( classes.begin(), classes.end(), What_If::has_service_time( var ) )) != classes.end() ) {
		_output << XML::attribute( XstationName, m->first )
			<< XML::attribute( XclassName, d->first )
			<< XML::attribute( Xtype, XService_Demands );
	    } else if ( (d = std::find_if( classes.begin(), classes.end(), What_If::has_visits( var ) )) != classes.end() ) {
		_output << XML::attribute( XstationName, m->first )
			<< XML::attribute( XclassName, d->first )
			<< XML::attribute( Xtype, "Visits" );
	    } else {
		abort();
	    }
	} else {
	    /* Var not found! */
	    _output << "<!-- Var not found: " << var << " -->" << std::endl;
	    return;
	}

	/* Print it out */

	std::ostringstream values;
	std::deque<Comprehension>::const_iterator whatif = std::find_if( whatif_statements().begin(), whatif_statements().end(), Comprehension::find(var) );
	if ( whatif != whatif_statements().end() ) {
	    /* Simple, run the comprehension directly */
	    values << *whatif;
	}
	_output << XML::attribute( Xvalues, values.str() );
	_output << "/>  <!--" << var << "-->" << std::endl;
    }

    bool
    JMVA_Document::What_If::match( const LQX::SyntaxTreeNode * var, const std::string& s )
    {
	return var != nullptr && dynamic_cast<const LQX::VariableExpression *>(var) && dynamic_cast<const LQX::VariableExpression *>(var)->getName() == s;
    }


    /* Return true if this class has the variable */

    bool
    JMVA_Document::What_If::has_customers::operator()( const BCMP::Model::Chain::pair_t& k ) const
    {
	if ( !k.second.isClosed() ) return false;
	return match( k.second.customers(), _var );
    }

    bool
    JMVA_Document::What_If::has_arrival_rate::operator()( const BCMP::Model::Chain::pair_t& k ) const
    {
	if ( !k.second.isOpen() ) return false;
	return match( k.second.arrival_rate(), _var );
    }

    bool
    JMVA_Document::What_If::has_copies::operator()( const BCMP::Model::Station::pair_t& m ) const
    {
	return match( m.second.copies(), _var );
    }

    /* Search for the variable in all classes */

    bool
    JMVA_Document::What_If::has_var::operator()( const BCMP::Model::Station::pair_t& m ) const
    {
	const BCMP::Model::Station::Class::map_t& classes = m.second.classes();
	return std::any_of( classes.begin(), classes.end(), What_If::has_service_time( _var ) )
	    || std::any_of( classes.begin(), classes.end(), What_If::has_visits( _var ) );
    }

    bool
    JMVA_Document::What_If::has_service_time::operator()( const BCMP::Model::Station::Class::pair_t& d ) const
    {
	return match( d.second.service_time(), _var );
    }

    bool
    JMVA_Document::What_If::has_visits::operator()( const BCMP::Model::Station::Class::pair_t& d ) const
    {
	return match( d.second.visits(), _var );
    }

    std::string
    JMVA_Document::fold( const std::string& s1, const var_name_and_expr& v2 )
    {
	if ( !s1.empty() ) {
	    return s1 + ";" + v2.first;
	} else {
	    return v2.first;
	}
    }
}

namespace QNIO
{
    /*
     * Construct the LQX program if one wasn't present.
     */

    LQX::Program *
    JMVA_Document::getLQXProgram()
    {

	if ( _lqx != nullptr ) return _lqx;

	/* insert the actual program (the loops) */

	if ( !_gnuplot.empty() ) {
	    LQIO::GnuPlot::insert_header( &_program, model().comment(), _result_variables );
	} else {
	    _program.push_back( print_csv_header() );
	}
	_program.push_back( new LQX::AssignmentStatementNode( new LQX::VariableExpression( "_0", false ), new LQX::ConstantValueExpression( 0. ) ) );
	_program.push_back( foreach_loop( whatif_statements().begin(), whatif_statements().end() ) );

	/*+ gnuplo t-> append the gnuplot program. */
	if ( !_gnuplot.empty() ) {
	    _program.push_back( LQIO::GnuPlot::print_node( "EOF" ) );
	    _program.insert( _program.end(), _gnuplot.begin(), _gnuplot.end() );
	}

	/* Convert and return */

	_lqx = LQX::Program::loadRawProgram( &_program );
	return _lqx;
    }

    /* Creat a for-loop for each whatif */

    LQX::SyntaxTreeNode *
    JMVA_Document::foreach_loop( std::deque<Comprehension>::const_iterator comprehension, std::deque<Comprehension>::const_iterator end ) const
    {
	if ( comprehension != end ) {
	    LQX::SyntaxTreeNode * expr = foreach_loop( std::next( comprehension ), end );
	    std::vector<LQX::SyntaxTreeNode *>* loop_body = new std::vector<LQX::SyntaxTreeNode *>();
	    loop_body->push_back( expr );
	    return comprehension->collect( loop_body );
	} else {
	    return new LQX::CompoundStatementNode( this->loop_body() );
	}
	return nullptr;
    }


    /*
     * Code which is run in the innermost iteration of the experiments, i.e., for () { for () { loop_body() } } ;
     * The guts of the execution.
     */

    std::vector<LQX::SyntaxTreeNode *>*
    JMVA_Document::loop_body() const
    {
	std::vector<LQX::SyntaxTreeNode *>* loop_code = new std::vector<LQX::SyntaxTreeNode *>();
	loop_code->push_back( new LQX::AssignmentStatementNode( new LQX::VariableExpression( "_0", false ),
								new LQX::MathExpression(LQX::MathExpression::ADD, new LQX::VariableExpression( "_0", false ), new LQX::ConstantValueExpression( 1.0 ) ) ) );
	loop_code->insert( loop_code->end(), _whatif_body.begin(), _whatif_body.end() );
	loop_code->push_back( new LQX::ConditionalStatementNode( new LQX::MethodInvocationExpression("solve"),
								 new LQX::CompoundStatementNode( solve_success() ),
								 new LQX::CompoundStatementNode( solve_failure() ) ) );

	return loop_code;
    }

    /*
     * Code which is run provided that the solver ran successfully.  This simply prints out the results.
     * !!! I need to check for unassigned variables. !!!
     */

    std::vector<LQX::SyntaxTreeNode *>* JMVA_Document::solve_success() const
    {
	std::vector<LQX::SyntaxTreeNode *>* block = new std::vector<LQX::SyntaxTreeNode *>;

	/* Insert all functions to extract results. */

	for ( std::vector<var_name_and_expr>::const_iterator result = _result_variables.begin(); result != _result_variables.end(); ++result ) {
	    block->push_back( result->second );
	}

	std::vector<std::vector<LQX::SyntaxTreeNode *>*> print_arguments;

	if ( _independent_variables.empty() && !_station_index.empty() ) {
	    /* No WhatIf and default output */
	    std::map<const std::string,const std::string>::const_iterator current_station = _station_index.end();
	    for ( std::vector<var_name_and_expr>::const_iterator result = _result_variables.begin(); result != _result_variables.end(); ++result ) {
		const std::map<const std::string,const std::string>::const_iterator next_station = _station_index.find( result->first );
		if ( next_station == _station_index.end() ) break;
		if ( next_station->second != current_station->second ) {
		    /* Station name changed so start a new output line */
		    current_station = next_station;
		    print_arguments.push_back( new std::vector<LQX::SyntaxTreeNode *> );				/* New row. */
		    print_arguments.back()->push_back( new LQX::ConstantValueExpression( ", " ) );			/* CSV. */
		    print_arguments.back()->push_back( new LQX::ConstantValueExpression( current_station->second ) );	/* Station name */
		}
		print_arguments.back()->push_back( new LQX::VariableExpression( result->first, false ) );		/* Add result. */
	    }

	} else {
	    print_arguments.push_back( new std::vector<LQX::SyntaxTreeNode *> );
	    print_arguments.back()->push_back( new LQX::ConstantValueExpression( ", " ) );				/* CSV. */
	    for ( std::vector<var_name_and_expr>::const_iterator result = _result_variables.begin(); result != _result_variables.end(); ++result ) {
		print_arguments.back()->push_back( new LQX::VariableExpression( result->first, false ) );		/* Print out results */
	    }
	}

	/* Insert print expression for results */

	for ( std::vector<std::vector<LQX::SyntaxTreeNode *>*>::const_iterator arguments = print_arguments.begin(); arguments != print_arguments.end(); ++arguments ) {
	    if ( (*arguments)->empty() ) continue;
	    block->push_back( new LQX::FilePrintStatementNode( (*arguments), true, true ) );	/* Force spaced output with newline */
	}

	return block;
    }


    /*
     * Sad panda.  The solver did not run successfully.
     */

    std::vector<LQX::SyntaxTreeNode *>* JMVA_Document::solve_failure() const
    {
	std::vector<LQX::SyntaxTreeNode *>* block = new std::vector<LQX::SyntaxTreeNode *>;
	block->push_back( LQIO::GnuPlot::print_node( new LQX::ConstantValueExpression( "solver failed: $0=" ), new LQX::VariableExpression( "_0", false ), nullptr ) );
	return block;
    }


    /*
     * Print out the header line for CSV output.
     */

    LQX::SyntaxTreeNode * JMVA_Document::print_csv_header() const
    {
	std::vector<LQX::SyntaxTreeNode *>* list = new std::vector<LQX::SyntaxTreeNode *>();
	list->push_back( new LQX::ConstantValueExpression( ", " ) );

	if ( _independent_variables.empty() && !_station_index.empty() ) {
	    /* Default... organize by station as rows and results as columns */
	    list->push_back( new LQX::ConstantValueExpression( "Station" ) );	/* Print out input variables */
	    std::for_each( JMVA_Document::__result_name_table.begin(), JMVA_Document::__result_name_table.end(), csv_heading( list, chains() ) );
	} else {
	    for ( std::vector<var_name_and_expr>::const_iterator var = _result_variables.begin(); var != _result_variables.end(); ++var ) {
		list->push_back( new LQX::ConstantValueExpression( var->first ) );	/* Variable name */
	    }
	}
	return new LQX::FilePrintStatementNode( list, true, true );		/* Println spaced, with first arg being ", " (or: output, ","). */
    }


    void
    JMVA_Document::csv_heading::operator()( const std::pair<const std::string,const BCMP::Model::Result::Type>& result )
    {
	if ( result.second == BCMP::Model::Result::Type::RESPONSE_TIME ) return;	// ignore chain results as they are not output
	if ( chains().size() > 1 ) {
	    for ( BCMP::Model::Chain::map_t::const_iterator k = chains().begin(); k != chains().end(); ++k ) {
		_arguments->push_back( new LQX::ConstantValueExpression( result.first + "(" + k->first  + ")" ) );
	    }
	}
	/* Total */
	_arguments->push_back( new LQX::ConstantValueExpression( result.first ) );
    }

    void
    JMVA_Document::notSet::getVariables( const JMVA_Document& document )
    {
	for ( const auto& var : document._arrival_rate_vars ) _variables.emplace( var.second );
	for ( const auto& var : document._population_vars ) _variables.emplace( var.second );
	for ( const auto& var : document._multiplicity_vars ) _variables.emplace( var.second );
	for ( const auto& var : document._service_time_vars ) _variables.emplace( var.second );
	for ( const auto& var : document._visit_vars ) _variables.emplace( var.second );
    }

    std::vector<std::string>
    JMVA_Document::notSet::operator()( const std::vector<std::string>& arg1, const std::string& arg2 ) const
    {
	if ( _variables.find(arg2) != _variables.end() ) return arg1;
	std::vector<std::string> result = arg1;
	result.push_back( arg2 );
	return result;
    }
}

namespace QNIO {
    using namespace LQIO;

    bool JMVA_Document::convertToLQN( DOM::Document& document ) const
    {
	return LQIO::DOM::BCMP_to_LQN( model(), document ).convert();
    }
}

namespace QNIO {
    /* Tables for input parsing */
    const std::set<const XML_Char *,JMVA_Document::attribute_table_t> JMVA_Document::algParams_table = { XmaxSamples, Xname, Xtolerance };
    const std::set<const XML_Char *,JMVA_Document::attribute_table_t> JMVA_Document::compareAlgs_table = { XmeanValue, XmeasureType, Xsuccessful };
    const std::set<const XML_Char *,JMVA_Document::attribute_table_t> JMVA_Document::null_table = {};

    /* Result type to lqx function name */

    const std::map<const BCMP::Model::Result::Type,const std::string> JMVA_Document::__lqx_function_table = {
	{ BCMP::Model::Result::Type::QUEUE_LENGTH,   BCMP::__lqx_queue_length },
	{ BCMP::Model::Result::Type::RESIDENCE_TIME, BCMP::__lqx_residence_time },
	{ BCMP::Model::Result::Type::RESPONSE_TIME,  BCMP::__lqx_response_time },
	{ BCMP::Model::Result::Type::THROUGHPUT,     BCMP::__lqx_throughput },
	{ BCMP::Model::Result::Type::UTILIZATION,    BCMP::__lqx_utilization }
    };

    /* Table for y label when plotting */
    const std::map<const BCMP::Model::Result::Type, const std::string> JMVA_Document::__y_label_table = {
	{ BCMP::Model::Result::Type::QUEUE_LENGTH,   JMVA_Document::XNumber_of_Customers },
	{ BCMP::Model::Result::Type::RESIDENCE_TIME, JMVA_Document::XResidence_Time },
	{ BCMP::Model::Result::Type::RESPONSE_TIME,  JMVA_Document::XResponse_Time  },
	{ BCMP::Model::Result::Type::THROUGHPUT,     JMVA_Document::XThroughput },
	{ BCMP::Model::Result::Type::UTILIZATION,    JMVA_Document::XUtilization }
    };

    const std::map<const std::string,const BCMP::Model::Result::Type> JMVA_Document::__result_name_table = {
	{"$Q", BCMP::Model::Result::Type::QUEUE_LENGTH},
	{"$X", BCMP::Model::Result::Type::THROUGHPUT},
	{"$T", BCMP::Model::Result::Type::RESPONSE_TIME},
	{"$R", BCMP::Model::Result::Type::RESIDENCE_TIME},
	{"$U", BCMP::Model::Result::Type::UTILIZATION}
    };


    /* Schema element/attribute names */
    const XML_Char * JMVA_Document::XArrivalProcess	= "Arrival Process";
    const XML_Char * JMVA_Document::XClass		= "Class";
    const XML_Char * JMVA_Document::XReferenceStation	= "ReferenceStation";
    const XML_Char * JMVA_Document::XalgParams		= "algParams";
    const XML_Char * JMVA_Document::XalgType		= "algType";
    const XML_Char * JMVA_Document::Xclass		= "class";
    const XML_Char * JMVA_Document::Xclasses		= "classes";
    const XML_Char * JMVA_Document::Xclosedclass	= "closedclass";
    const XML_Char * JMVA_Document::XcompareAlgs	= "compareAlgs";
    const XML_Char * JMVA_Document::Xcustomerclass	= "customerclass";
    const XML_Char * JMVA_Document::Xdelaystation	= "delaystation";
    const XML_Char * JMVA_Document::Xdescription	= "description";
    const XML_Char * JMVA_Document::Xjaba		= "jaba";
    const XML_Char * JMVA_Document::Xlistation		= "listation";
    const XML_Char * JMVA_Document::XLQX		= "lqx";
    const XML_Char * JMVA_Document::Xldstation		= "ldstation";
    const XML_Char * JMVA_Document::XmaxSamples		= "maxSamples";
    const XML_Char * JMVA_Document::Xmodel		= "model";
    const XML_Char * JMVA_Document::Xmultiplicity	= "multiplicity";
    const XML_Char * JMVA_Document::Xname		= "name";
    const XML_Char * JMVA_Document::Xnumber		= "number";
    const XML_Char * JMVA_Document::Xopenclass		= "openclass";
    const XML_Char * JMVA_Document::Xparam		= "param";
    const XML_Char * JMVA_Document::Xparameters		= "parameters";
    const XML_Char * JMVA_Document::Xpopulation		= "population";
    const XML_Char * JMVA_Document::Xpragma		= "pragma";
    const XML_Char * JMVA_Document::Xrate		= "rate";
    const XML_Char * JMVA_Document::XrefStation		= "refStation";
    const XML_Char * JMVA_Document::Xservers		= "servers";
    const XML_Char * JMVA_Document::Xservicetime	= "servicetime";
    const XML_Char * JMVA_Document::Xservicetimes	= "servicetimes";
    const XML_Char * JMVA_Document::Xstation		= "station";
    const XML_Char * JMVA_Document::Xstations		= "stations";
    const XML_Char * JMVA_Document::Xthinktime 		= "thinktime";
    const XML_Char * JMVA_Document::Xtolerance		= "tolerance";
    const XML_Char * JMVA_Document::Xvalue		= "value";
    const XML_Char * JMVA_Document::Xvisit		= "visit";
    const XML_Char * JMVA_Document::Xvisits		= "visits";
    const XML_Char * JMVA_Document::XwhatIf		= "whatIf";
    const XML_Char * JMVA_Document::Xxml_debug 		= "xml-debug";

    const XML_Char * JMVA_Document::Xalgorithm 		= "algorithm";
    const XML_Char * JMVA_Document::Xclassresults	= "classresults";
    const XML_Char * JMVA_Document::XmeanValue		= "meanValue";
    const XML_Char * JMVA_Document::Xmeasure		= "measure";
    const XML_Char * JMVA_Document::XmeasureType        = "measureType";
    const XML_Char * JMVA_Document::Xsolutions 		= "solutions";
    const XML_Char * JMVA_Document::Xstationresults	= "stationresults";
    const XML_Char * JMVA_Document::Xsuccessful		= "successful";

    const XML_Char * JMVA_Document::XclassName		= "className";
    const XML_Char * JMVA_Document::XstationName	= "stationName";
    const XML_Char * JMVA_Document::Xtype		= "type";
    const XML_Char * JMVA_Document::Xvalues		= "values";

    const XML_Char * JMVA_Document::XalgCount 		= "algCount";
    const XML_Char * JMVA_Document::Xfalse		= "false";
    const XML_Char * JMVA_Document::Xiteration		= "iteration";
    const XML_Char * JMVA_Document::XiterationValue	= "iterationValue";
    const XML_Char * JMVA_Document::Xiterations		= "iterations";
    const XML_Char * JMVA_Document::Xok			= "ok";
    const XML_Char * JMVA_Document::XsolutionMethod   	= "solutionMethod";
    const XML_Char * JMVA_Document::Xtrue		= "true";

    const XML_Char * JMVA_Document::XArrival_Rates	= "Arrival Rates";
    const XML_Char * JMVA_Document::XCustomer_Numbers 	= "Customer Numbers";
    const XML_Char * JMVA_Document::XNumberOfCustomers	= "NumberOfCustomers";
    const XML_Char * JMVA_Document::XNumber_of_Customers= "Number of Customers";
    const XML_Char * JMVA_Document::XNumber_of_Servers	= "Number of Servers";
    const XML_Char * JMVA_Document::XPopulation_Mix     = "Population Mix";
    const XML_Char * JMVA_Document::XResidenceTime      = "ResidenceTime";
    const XML_Char * JMVA_Document::XResidence_Time     = "Residence time";
    const XML_Char * JMVA_Document::XResponse_Time      = "Response Time";
    const XML_Char * JMVA_Document::XService_Demands	= "Service Demands";
    const XML_Char * JMVA_Document::XThroughput         = "Throughput";
    const XML_Char * JMVA_Document::XUtilization        = "Utilization";
    const XML_Char * JMVA_Document::Xnormconst          = "normconst";
}
