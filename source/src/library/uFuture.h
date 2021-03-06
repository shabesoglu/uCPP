//                              -*- Mode: C++ -*- 
// 
// uC++ Version 6.1.0, Copyright (C) Peter A. Buhr and Richard C. Bilson 2006
// 
// Future.h -- 
// 
// Author           : Peter A. Buhr and Richard C. Bilson
// Created On       : Wed Aug 30 22:34:05 2006
// Last Modified By : Peter A. Buhr
// Last Modified On : Sat Apr 30 21:49:15 2016
// Update Count     : 623
// 
// This  library is free  software; you  can redistribute  it and/or  modify it
// under the terms of the GNU Lesser General Public License as published by the
// Free Software  Foundation; either  version 2.1 of  the License, or  (at your
// option) any later version.
// 
// This library is distributed in the  hope that it will be useful, but WITHOUT
// ANY  WARRANTY;  without even  the  implied  warranty  of MERCHANTABILITY  or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
// for more details.
// 
// You should  have received a  copy of the  GNU Lesser General  Public License
// along  with this library.
// 

#ifndef __U_FUTURE_H__
#define __U_FUTURE_H__


//############################## uBaseFuture ##############################


namespace UPP {
    template<typename T> _Monitor uBaseFuture {
	T result;					// future result
      public:
	_Event Cancellation {};				// raised if future cancelled

	// These members should be private but must be referenced from code generated by the translator.

	bool addSelect( UPP::BaseFutureDL *selectState ) {
	    if ( ! available() ) {
		selectClients.addTail( selectState );
	    } // if
	    return available();
	} // uBaseFuture::addSelect

	void removeSelect( UPP::BaseFutureDL *selectState ) {
	    selectClients.remove( selectState );
	} // uBaseFuture::removeSelect
      protected:
	uCondition delay;				// clients waiting for future result
	uSequence<UPP::BaseFutureDL> selectClients;	// clients waiting for future result in selection
	uBaseEvent *cause;				// synchronous exception raised during future computation
	bool available_, cancelled_;			// future status

	void makeavailable() {
	    available_ = true;
	    while ( ! delay.empty() ) delay.signal();	// unblock waiting clients ?
	    if ( ! selectClients.empty() ) {		// select-blocked clients ?
		UPP::BaseFutureDL *bt;			// unblock select-blocked clients
		for ( uSeqIter<UPP::BaseFutureDL> iter( selectClients ); iter >> bt; ) {
		    bt->signal();
		} // for
	    } // if
	} // uBaseFuture::makeavailable

	void check() {
	    if ( cancelled() ) _Throw Cancellation();
	    if ( cause != NULL ) cause->reraise();
	} // uBaseFuture::check
      public:
	uBaseFuture() : cause( NULL ), available_( false ), cancelled_( false ) {}

	_Nomutex bool available() { return available_; } // future result available ?
	_Nomutex bool cancelled() { return cancelled_; } // future result cancelled ?

	// USED BY CLIENT

	T operator()() {				// access result, possibly having to wait
	    check();					// cancelled or exception ?
	    if ( ! available() ) {
		delay.wait();
		check();				// cancelled or exception ?
	    } // if
	    return result;
	} // uBaseFuture::operator()()

	_Nomutex operator T() {				// cheap access of result after waiting
	    check();					// cancelled or exception ?
#ifdef __U_DEBUG__
	    if ( ! available() ) {
		uAbort( "Attempt to access future result %p without first performing a blocking access operation.", this );
	    } // if
#endif // __U_DEBUG__
	    return result;
	} // uBaseFuture::operator T()

	// USED BY SERVER

	bool delivery( T res ) {			// make result available in the future
	    if ( cancelled() || available() ) return false; // ignore, client does not want it or already set
	    result = res;
	    makeavailable();
	    return true;
	} // uBaseFuture::delivery

	bool exception( uBaseEvent *ex ) {		// make exception available in the future : exception and result mutual exclusive
	    if ( cancelled() || available() ) return false; // ignore, client does not want it or already set
	    cause = ex;
	    makeavailable();				// unblock waiting clients ?
	    return true;
	} // uBaseFuture::exception

	void reset() {					// mark future as empty (for reuse)
#ifdef __U_DEBUG__
	    if ( ! delay.empty() || ! selectClients.empty() ) {
		uAbort( "Attempt to reset future %p with waiting tasks.", this );
	    } // if
#endif // __U_DEBUG__
	    available_ = cancelled_ = false;		// reset for next value
	    delete cause;
	    cause = NULL;
	} // uBaseFuture::reset
    }; // uBaseFuture
} // UPP


//############################## Future_ESM ##############################


// Caller is responsible for storage management by preallocating the future and passing it as an argument to the
// asynchronous call.  Cannot be copied.

template<typename T, typename ServerData> _Monitor Future_ESM : public UPP::uBaseFuture<T> {
    using UPP::uBaseFuture<T>::cancelled_;
    bool cancelInProgress;

    void makeavailable() {
	cancelInProgress = false;
	cancelled_ = true;
	UPP::uBaseFuture<T>::makeavailable();
    } // Future_ESM::makeavailable

    _Mutex int checkCancel() {
      if ( available() ) return 0;			// already available, can't cancel
      if ( cancelled() ) return 0;			// only cancel once
      if ( cancelInProgress ) return 1;
	cancelInProgress = true;
	return 2;
    } // Future_ESM::checkCancel

    _Mutex void compCancelled() {
	makeavailable();
    } // Future_ESM::compCancelled

    _Mutex void compNotCancelled() {
	// Race by server to deliver and client to cancel.  While the future is already cancelled, the server can
	// attempt to signal (unblock) this client before the client can block, so the signal is lost.
	if ( cancelInProgress ) {			// must recheck
	    delay.wait();				// wait for cancellation
	} // if
    } // Future_ESM::compNotCancelled
  public:
    using UPP::uBaseFuture<T>::available;
    using UPP::uBaseFuture<T>::reset;
    using UPP::uBaseFuture<T>::makeavailable;
    using UPP::uBaseFuture<T>::check;
    using UPP::uBaseFuture<T>::delay;
    using UPP::uBaseFuture<T>::cancelled;

    Future_ESM() : cancelInProgress( false ) {}
    ~Future_ESM() { reset(); }

    // USED BY CLIENT

    _Nomutex void cancel() {				// cancel future result
	// To prevent deadlock, call the server without holding future mutex, because server may attempt to deliver a
	// future value. (awkward code)
	unsigned int rc = checkCancel();
      if ( rc == 0 ) return;				// need to contact server ?
	if ( rc == 1 ) {				// need to contact server ?
	    compNotCancelled();				// server computation not cancelled yet, wait for cancellation
	} else {
	    if ( serverData.cancel() ) {		// synchronously contact server
		compCancelled();			// computation cancelled, announce cancellation
	    } else {
		compNotCancelled();			// server computation not cancelled yet, wait for cancellation
	    } // if
	} // if
    } // Future_ESM::cancel

    // USED BY SERVER

    ServerData serverData;				// information needed by server

    bool delivery( T res ) {				// make result available in the future
	if ( cancelInProgress ) {
	    makeavailable();
	    return true;
	} else {
	    return UPP::uBaseFuture<T>::delivery( res );
	} // if
    } // Future_ESM::delivery

    bool exception( uBaseEvent *ex ) {			// make exception available in the future : exception and result mutual exclusive
	if ( cancelInProgress ) {
	    makeavailable();
	    return true;
	} else {
	    return UPP::uBaseFuture<T>::exception( ex );
	} // if
    } // Future_ESM::exception
}; // Future_ESM


// handler 4 cases for || and &&: future operator future, future operator binary, binary operator future, binary operator binary

template< typename Future1, typename ServerData1, typename Future2, typename ServerData2 >
UPP::BinarySelector< UPP::UnarySelector< Future_ESM< Future1, ServerData1 > >, UPP::UnarySelector< Future_ESM< Future2, ServerData2 > > > operator||( Future_ESM< Future1, ServerData1 > &f1, Future_ESM< Future2, ServerData2 > &f2 ) {
    //osacquire( cerr ) << "ESM< Future1, Future2 >, Or( f1 " << &f1 << ", f2 " << &f2 << " )" << endl;
    return UPP::BinarySelector< UPP::UnarySelector< Future_ESM< Future1, ServerData1 > >, UPP::UnarySelector< Future_ESM< Future2, ServerData2 > > >( UPP::UnarySelector< Future_ESM< Future1, ServerData1 > >( f1 ), UPP::UnarySelector< Future_ESM< Future2, ServerData2 > >( f2 ), UPP::Condition::Or );
} // operator||

template< typename Left, typename Right, typename Future, typename ServerData >
UPP::BinarySelector< UPP::BinarySelector< Left, Right >, UPP::UnarySelector< Future_ESM< Future, ServerData > > > operator||( UPP::BinarySelector< Left, Right > bs, Future_ESM< Future, ServerData > &f ) {
    //osacquire( cerr ) << "ESM< BinarySelector, Future >, Or( bs " << &bs << ", f " << &f << " )" << endl;
    return UPP::BinarySelector< UPP::BinarySelector< Left, Right >, UPP::UnarySelector< Future_ESM< Future, ServerData > > >( bs, UPP::UnarySelector< Future_ESM< Future, ServerData > >( f ), UPP::Condition::Or );
} // operator||

template< typename Future, typename ServerData, typename Left, typename Right >
UPP::BinarySelector< UPP::UnarySelector< Future_ESM< Future, ServerData > >, UPP::BinarySelector< Left, Right > > operator||( Future_ESM< Future, ServerData > &f, UPP::BinarySelector< Left, Right > bs ) {
    //osacquire( cerr ) << "ESM< Future, BinarySelector >, Or( f " << &f << ", bs " << &bs << " )" << endl;
    return UPP::BinarySelector< UPP::UnarySelector< Future_ESM< Future, ServerData > >, UPP::BinarySelector< Left, Right > >( UPP::UnarySelector< Future_ESM< Future, ServerData > >( f ), bs, UPP::Condition::Or );
} // operator||


template< typename Future1, typename ServerData1, typename Future2, typename ServerData2 >
UPP::BinarySelector< UPP::UnarySelector< Future_ESM< Future1, ServerData1 > >, UPP::UnarySelector< Future_ESM< Future2, ServerData2 > > > operator&&( Future_ESM< Future1, ServerData1 > &f1, Future_ESM< Future2, ServerData2 > &f2 ) {
    //osacquire( cerr ) << "ESM< Future1, Future2 >, And( f1 " << &f1 << ", f2 " << &f2 << " )" << endl;
    return UPP::BinarySelector< UPP::UnarySelector< Future_ESM< Future1, ServerData1 > >, UPP::UnarySelector< Future_ESM< Future2, ServerData2 > > >( UPP::UnarySelector< Future_ESM< Future1, ServerData1 > >( f1 ), UPP::UnarySelector< Future_ESM< Future2, ServerData2 > >( f2 ), UPP::Condition::And );
} // operator&&

template< typename Left, typename Right, typename Future, typename ServerData >
UPP::BinarySelector< UPP::BinarySelector< Left, Right >, UPP::UnarySelector< Future_ESM< Future, ServerData > > > operator&&( UPP::BinarySelector< Left, Right > bs, Future_ESM< Future, ServerData > &f ) {
    //osacquire( cerr ) << "ESM< BinarySelector, Future >, And( bs " << &bs << ", f " << &f << " )" << endl;
    return UPP::BinarySelector< UPP::BinarySelector< Left, Right >, UPP::UnarySelector< Future_ESM< Future, ServerData > > >( bs, UPP::UnarySelector< Future_ESM< Future, ServerData > >( f ), UPP::Condition::And );
} // operator&&

template< typename Future, typename ServerData, typename Left, typename Right >
UPP::BinarySelector< UPP::UnarySelector< Future_ESM< Future, ServerData > >, UPP::BinarySelector< Left, Right > > operator&&( Future_ESM< Future, ServerData > &f, UPP::BinarySelector< Left, Right > bs ) {
    //osacquire( cerr ) << "ESM< Future, BinarySelector >, And( f " << &f << ", bs " << &bs << " )" << endl;
    return UPP::BinarySelector< UPP::UnarySelector< Future_ESM< Future, ServerData > >, UPP::BinarySelector< Left, Right > >( UPP::UnarySelector< Future_ESM< Future, ServerData > >( f ), bs, UPP::Condition::And );
} // operator&&


// shared by ESM and ISM

template< typename Left1, typename Right1, typename Left2, typename Right2 >
UPP::BinarySelector< UPP::BinarySelector< Left1, Right1 >, UPP::BinarySelector< Left2, Right2 > > operator||( UPP::BinarySelector< Left1, Right1 > bs1, UPP::BinarySelector< Left2, Right2 > bs2 ) {
    //osacquire( cerr ) << "ESM/ISM< Future, BinarySelector >, Or( bs1 " << &bs1 << ", bs2 " << &bs2 << " )" << endl;
    return UPP::BinarySelector< UPP::BinarySelector< Left1, Right1 >, UPP::BinarySelector< Left2, Right2 > >( bs1, bs2, UPP::Condition::Or );
} // operator||

template< typename Left1, typename Right1, typename Left2, typename Right2 >
UPP::BinarySelector< UPP::BinarySelector< Left1, Right1 >, UPP::BinarySelector< Left2, Right2 > > operator&&( UPP::BinarySelector< Left1, Right1 > bs1, UPP::BinarySelector< Left2, Right2 > bs2 ) {
    //osacquire( cerr ) << "ESM/ISM< Future, BinarySelector >, And( bs1 " << &bs1 << ", bs2 " << &bs2 << " )" << endl;
    return UPP::BinarySelector< UPP::BinarySelector< Left1, Right1 >, UPP::BinarySelector< Left2, Right2 > >( bs1, bs2, UPP::Condition::And );
} // operator&&


//############################## Future_ISM ##############################


// Future is responsible for storage management by using reference counts.  Can be copied.

template<typename T> class Future_ISM {
  public:
    struct ServerData {
	virtual ~ServerData() {}
	virtual bool cancel() = 0;
    };
  private:
    _Monitor Impl : public UPP::uBaseFuture<T> {	// mutual exclusion implementation
	using UPP::uBaseFuture<T>::cancelled_;
	using UPP::uBaseFuture<T>::cause;

	unsigned int refCnt;				// number of references to future
	ServerData *serverData;
      public:
	using UPP::uBaseFuture<T>::available;
	using UPP::uBaseFuture<T>::reset;
	using UPP::uBaseFuture<T>::makeavailable;
	using UPP::uBaseFuture<T>::check;
	using UPP::uBaseFuture<T>::delay;
	using UPP::uBaseFuture<T>::cancelled;

	Impl() : refCnt( 1 ), serverData( NULL ) {}
	Impl( ServerData *serverData_ ) : refCnt( 1 ), serverData( serverData_ ) {}

	~Impl() {
	    delete serverData;
	} // Impl::~Impl

	void incRef() {
	    refCnt += 1;
	} // Impl::incRef

	bool decRef() {
	    refCnt -= 1;
	  if ( refCnt != 0 ) return false;
	    delete cause;
	    return true;
	} // Impl::decRef

	void cancel() {					// cancel future result
	  if ( available() ) return;			// already available, can't cancel
	  if ( cancelled() ) return;			// only cancel once
	    cancelled_ = true;
	    if ( serverData != NULL ) serverData->cancel();
	    makeavailable();				// unblock waiting clients ?
	} // Impl::cancel
    }; // Impl

    Impl *impl;						// storage for implementation
  public:
    Future_ISM() : impl( new Impl ) {}
    Future_ISM( ServerData *serverData ) : impl( new Impl( serverData ) ) {}

    ~Future_ISM() {
	if ( impl->decRef() ) delete impl;
    } // Future_ISM::~Future_ISM

    Future_ISM( const Future_ISM<T> &rhs ) {
	impl = rhs.impl;				// point at new impl
	impl->incRef();					//   and increment reference count
    } // Future_ISM::Future_ISM

    Future_ISM<T> &operator=( const Future_ISM<T> &rhs ) {
      if ( rhs.impl == impl ) return *this;
	if ( impl->decRef() ) delete impl;		// no references => delete current impl
	impl = rhs.impl;				// point at new impl
	impl->incRef();					//   and increment reference count
	return *this;
    } // Future_ISM::operator=

    // USED BY CLIENT

    typedef typename UPP::uBaseFuture<T>::Cancellation Cancellation; // raised if future cancelled

    bool available() { return impl->available(); }	// future result available ?
    bool cancelled() { return impl->cancelled(); }	// future result cancelled ?

    T operator()() {					// access result, possibly having to wait
	return (*impl)();
    } // Future_ISM::operator()()

    operator T() {					// cheap access of result after waiting
	return (T)(*impl);
    } // Future_ISM::operator T()

    void cancel() {					// cancel future result
	impl->cancel();
    } // Future_ISM::cancel

    bool addSelect( UPP::BaseFutureDL *selectState ) {
	return impl->addSelect( selectState );
    } // Future_ISM::addSelect

    void removeSelect( UPP::BaseFutureDL *selectState ) {
	return impl->removeSelect( selectState );
    } // Future_ISM::removeSelect

    bool equals( const Future_ISM<T> &other ) {		// referential equality
	return impl == other.impl;
    } // Future_ISM::equals

    // USED BY SERVER

    bool delivery( T result ) {				// make result available in the future
	return impl->delivery( result );
    } // Future_ISM::delivery

    bool exception( uBaseEvent *cause ) {		// make exception available in the future
	return impl->exception( cause );
    } // Future_ISM::exception

    void reset() {					// mark future as empty (for reuse)
	impl->reset();
    } // Future_ISM::reset
}; // Future_ISM


// handler 3 cases for || and &&: future operator future, future operator binary, binary operator future, binary operator binary

template< typename Future1, typename Future2 >
UPP::BinarySelector< UPP::UnarySelector< Future_ISM< Future1 > >, UPP::UnarySelector< Future_ISM< Future2 > > > operator||( Future_ISM< Future1 > &f1, Future_ISM< Future2 > &f2 ) {
    //osacquire( cerr ) << "ISM< Future1, Future2 >, Or( f1 " << &f1 << ", f2 " << &f2 << " )" << endl;
    return UPP::BinarySelector< UPP::UnarySelector< Future_ISM< Future1 > >, UPP::UnarySelector< Future_ISM< Future2 > > >( UPP::UnarySelector< Future_ISM< Future1 > >( f1 ), UPP::UnarySelector< Future_ISM< Future2 > >( f2 ), UPP::Condition::Or );
} // operator||

template< typename Left, typename Right, typename Future >
UPP::BinarySelector< UPP::BinarySelector< Left, Right >, UPP::UnarySelector< Future_ISM< Future > > > operator||( UPP::BinarySelector< Left, Right > bs, Future_ISM< Future > &f ) {
    //osacquire( cerr ) << "ISM< BinarySelector, Future >, Or( bs " << &bs << ", f " << &f << " )" << endl;
    return UPP::BinarySelector< UPP::BinarySelector< Left, Right >, UPP::UnarySelector< Future_ISM< Future > > >( bs, UPP::UnarySelector< Future_ISM< Future > >( f ), UPP::Condition::Or );
} // operator||

template< typename Future, typename Left, typename Right >
UPP::BinarySelector< UPP::UnarySelector< Future_ISM< Future > >, UPP::BinarySelector< Left, Right > > operator||( Future_ISM< Future > &f, UPP::BinarySelector< Left, Right > bs ) {
    //osacquire( cerr ) << "ISM< Future, BinarySelector >, Or( f " << &f << ", bs " << &bs << " )" << endl;
    return UPP::BinarySelector< UPP::UnarySelector< Future_ISM< Future > >, UPP::BinarySelector< Left, Right > >( UPP::UnarySelector< Future_ISM< Future > >( f ), bs, UPP::Condition::Or );
} // operator||


template< typename Future1, typename Future2 >
UPP::BinarySelector< UPP::UnarySelector< Future_ISM< Future1 > >, UPP::UnarySelector< Future_ISM< Future2 > > > operator&&( Future_ISM< Future1 > &f1, Future_ISM< Future2 > &f2 ) {
    //osacquire( cerr ) << "ISM< Future1, Future2 >, And( f1 " << &f1 << ", f2 " << &f2 << " )" << endl;
    return UPP::BinarySelector< UPP::UnarySelector< Future_ISM< Future1 > >, UPP::UnarySelector< Future_ISM< Future2 > > >( UPP::UnarySelector< Future_ISM< Future1 > >( f1 ), UPP::UnarySelector< Future_ISM< Future2 > >( f2 ), UPP::Condition::And );
} // operator&&

template< typename Left, typename Right, typename Future >
UPP::BinarySelector< UPP::BinarySelector< Left, Right >, UPP::UnarySelector< Future_ISM< Future > > > operator&&( UPP::BinarySelector< Left, Right > bs, Future_ISM< Future > &f ) {
    //osacquire( cerr ) << "ISM< BinarySelector, Future >, And( bs " << &bs << ", f " << &f << " )" << endl;
    return UPP::BinarySelector< UPP::BinarySelector< Left, Right >, UPP::UnarySelector< Future_ISM< Future > > >( bs, UPP::UnarySelector< Future_ISM< Future > >( f ), UPP::Condition::And );
} // operator&&

template< typename Future, typename Left, typename Right >
UPP::BinarySelector< UPP::UnarySelector< Future_ISM< Future > >, UPP::BinarySelector< Left, Right > > operator&&( Future_ISM< Future > &f, UPP::BinarySelector< Left, Right > bs ) {
    //osacquire( cerr ) << "ISM< Future, BinarySelector >, And( f " << &f << ", bs " << &bs << " )" << endl;
    return UPP::BinarySelector< UPP::UnarySelector< Future_ISM< Future > >, UPP::BinarySelector< Left, Right > >( UPP::UnarySelector< Future_ISM< Future > >( f ), bs, UPP::Condition::And );
} // operator&&


//############################## uWaitQueue_ISM ##############################


template< typename Selectee >
class uWaitQueue_ISM {
    struct DL;

    struct DropClient {
	UPP::uSemaphore sem; 				// selection client waits if no future available
	unsigned int tst;				// test-and-set for server race
	DL *winner;					// indicate winner of race

	DropClient() : sem( 0 ), tst( 0 ) {};
    }; // DropClient

    struct DL : public uSeqable {
	struct uBaseFutureDL : public UPP::BaseFutureDL {
	    DropClient *client;				// client data for server
	    DL *s;					// iterator corresponding to this DL

	    uBaseFutureDL( DL *t ) : s( t ) {}

	    virtual void signal() {
		if ( uTestSet( client->tst ) == 0 ) {	// returns 0 or non-zero
		    client->winner = s;
		    client->sem.V();			// client see changes because semaphore does memory barriers
		} // if
	    } // signal
	}; // uBaseFutureDL

	uBaseFutureDL selectState;
	Selectee selectee;

	DL( Selectee t ) : selectState( this ), selectee( t ) {}
    }; // DL

    uSequence< DL > q;

    uWaitQueue_ISM( const uWaitQueue_ISM & );		// no copy
    uWaitQueue_ISM &operator=( const uWaitQueue_ISM & ); // no assignment
  public:
    uWaitQueue_ISM() {}

    template< typename Iterator > uWaitQueue_ISM( Iterator begin, Iterator end ) {
	add( begin, end );
    } // uWaitQueue_ISM::uWaitQueue_ISM

    ~uWaitQueue_ISM() {
	DL *t;
	for ( uSeqIter< DL > i( q ); i >> t; ) {
	    delete t;
	} // for
    } // uWaitQueue_ISM::~uWaitQueue_ISM

    bool empty() const {
	return q.empty();
    } // uWaitQueue_ISM::empty

    void add( Selectee n ) {
	q.add( new DL( n ) );
    } // uWaitQueue_ISM::add

    template< typename Iterator > void add( Iterator begin, Iterator end ) {
	for ( Iterator i = begin; i != end; ++i ) {
	    add( *i );
	} // for
    } // uWaitQueue_ISM::add

    void remove( Selectee n ) {
	DL *t = 0;
	for ( uSeqIter< DL > i( q ); i >> t; ) {
	    if ( t->selectee.equals( n ) ) {
		q.remove( t );
		delete t;
	    } // if
	} // for
    } // uWaitQueue_ISM::remove

    Selectee drop() {
	if ( q.empty() ) uAbort( "uWaitQueue_ISM: attempt to drop from an empty queue" );

	DropClient client;
	DL *t = 0;
	for ( uSeqIter< DL > i( q ); i >> t; ) {
	    t->selectState.client = &client;
	    if ( t->selectee.addSelect( &t->selectState ) ) {
		DL *s;
		for ( uSeqIter< DL > i( q ); i >> s && s != t; ) {
		    s->selectee.removeSelect( &s->selectState );
		} // for
		goto cleanup;
	    } // if
	} // for

	client.sem.P();
	t = client.winner;
	DL *s;
	for ( uSeqIter< DL > i( q ); i >> s; ) {
	    s->selectee.removeSelect( &s->selectState );
	} // for

      cleanup:
	Selectee selectee = t->selectee;
	q.remove( t );
	delete t;
	return selectee;
    } // uWaitQueue_ISM::drop

    // not implemented, since the "head" of the queue is not fixed i.e., if another item comes ready it may become the
    // new "head" use "drop" instead
    //T *head() const;
}; // uWaitQueue_ISM


//############################## uWaitQueue_ESM ##############################


template< typename Selectee >
class uWaitQueue_ESM {
    struct Helper {
	Selectee *s;
	Helper( Selectee *s ) : s( s ) {}
	bool available() const { return s->available(); }
	bool addSelect( UPP::BaseFutureDL *selectState ) { return s->addSelect( selectState ); }
	void removeSelect( UPP::BaseFutureDL *selectState ) { return s->removeSelect( selectState ); }
	bool equals( const Helper &other ) const { return s == other.s; }
    }; // Helper

    uWaitQueue_ISM< Helper > q;

    uWaitQueue_ESM( const uWaitQueue_ESM & );		// no copy
    uWaitQueue_ESM &operator=( const uWaitQueue_ESM & ); // no assignment
  public:
    uWaitQueue_ESM() {}

    template< typename Iterator > uWaitQueue_ESM( Iterator begin, Iterator end ) {
	add( begin, end );
    } // uWaitQueue_ESM::uWaitQueue_ESM

    bool empty() const {
	return q.empty();
    } // uWaitQueue_ESM::empty

    void add( Selectee *n ) {
	q.add( Helper( n ) );
    } // uWaitQueue_ESM::add

    template< typename Iterator > void add( Iterator begin, Iterator end ) {
	for ( Iterator i = begin; i != end; ++i ) {
	    add( &*i );
	} // for
    } // uWaitQueue_ESM::add

    void remove( Selectee *s ) {
	q.remove( Helper( s ) );
    } // uWaitQueue_ESM::remove

    Selectee *drop() {
	return empty() ? 0 : q.drop().s;
    } // uWaitQueue_ESM::drop
}; // uWaitQueue_ESM


//############################## uExecutor ##############################


class uExecutor {
  public:
    enum Cluster { Same, Sep };				// use same or separate cluster
  private:
    // Mutex buffer is embedded in the nomutex executor to allow the executor to delete the workers without causing a
    // deadlock.  If the executor is the monitor and the buffer is class, the thread calling the executor's destructor
    // (which is mutex) blocks when deleting the workers, preventing outstanding workers from calling remove to drain
    // the buffer.
    template<typename ELEMTYPE> _Monitor Buffer {	// unbounded buffer
	uQueue<ELEMTYPE> buf;				// unbounded list of work requests
	uCondition delay;
      public:
	void insert( ELEMTYPE *elem ) {
	    buf.addTail( elem );
	    delay.signal();
	} // Buffer::insert

	ELEMTYPE *remove() {
	    if ( buf.empty() ) delay.wait();		// no request to process ? => wait
	    return buf.dropHead();
	} // Buffer::remove
    }; // Buffer

    struct WRequest : public uColable {			// worker request
	bool done;					// true => stop worker
	WRequest( bool done = false ) : done( done ) {}
	virtual ~WRequest() {};				// required for FRequest's result
	virtual bool stop() { return done; };
	virtual void doit() { assert( false ); };	// not abstract as used for sentinel
    }; // WRequest

    template<typename F> struct VRequest : public WRequest { // client request, no return
	F action;
	void doit() { action(); }
	VRequest( F action ) : action( action ) {}
    }; // VRequest

    template<typename R, typename F> struct FRequest : public WRequest { // client request, return
	F action;
	Future_ISM<R> result;
	void doit() { result.delivery( action() ); }
	FRequest( F action ) : action( action ) {}
    }; // FRequest

    _Task Worker {
	uExecutor &executor;

	void main() {
	    for ( ;; ) {
		WRequest *request = executor.requests.remove();
	      if ( request->stop() ) break;
		request->doit();
		delete request;
	    } // for
	} // Worker::main
      public:
	Worker( uCluster &wc, uExecutor &executor ) : uBaseTask( wc ), executor( executor ) {}
    }; // Worker

    enum { DefaultWorkers = 16, DefaultProcessors = 2 };
    const unsigned int nworkers, nprocessors;		// number of workers/processor tasks
    const Cluster clus;					// use same or separate cluster
    Worker **workers;					// array of workers executing work requests
    uProcessor **processors;				// array of virtual processors adding parallelism for workers
    uCluster *cluster;					// if workers execute on separate cluster
    Buffer<WRequest> requests;				// list of work requests
  public:
    uExecutor( unsigned int nworkers, unsigned int nprocessors, Cluster clus = Same ) : nworkers( nworkers ), nprocessors( nprocessors ), clus( clus ) {
	cluster = clus == Sep ? new uCluster : &uThisCluster();
	processors = new uProcessor *[ nprocessors ];
	workers = new Worker *[ nworkers ];

	for ( unsigned int i = 0; i < nprocessors; i += 1 ) {
	    processors[ i ] = new uProcessor( *cluster );
	} // for
	for ( unsigned int i = 0; i < nworkers; i += 1 ) {
	    workers[ i ] = new Worker( *cluster, *this );
	} // for
    } // uExecutor::uExecutor

    uExecutor( unsigned int nworkers, Cluster clus = Same ) : uExecutor( nworkers, DefaultProcessors, clus ) {}
    uExecutor( Cluster clus ) : uExecutor( DefaultWorkers, DefaultProcessors, clus ) {}
    uExecutor() : uExecutor( DefaultWorkers, DefaultProcessors, Same ) {}

    ~uExecutor() {
	// Add one sentinel per worker to stop them. Since in destructor, no new work should be queued.  Cannot combine
	// next two loops and only have a single sentinel because workers arrive in arbitrary order, so worker1 may take
	// the single sentinel while waiting for worker 0 to end.
	WRequest sentinel[nworkers];
	for ( unsigned int i = 0; i < nworkers; i += 1 ) {
	    sentinel[i].done = true;
	    requests.insert( &sentinel[i] );		// force eventually termination
	} // for
	for ( unsigned int i = 0; i < nworkers; i += 1 ) {
	    delete workers[ i ];
	} // for
	for ( unsigned int i = 0; i < nprocessors; i += 1 ) {
	    delete processors[ i ];
	} // for
	delete [] workers;
	delete [] processors;
	if ( clus == Sep ) delete cluster;
    } // uExecutor::~uExecutor

    template <typename Func> void send( Func action ) { // asynchronous call, no return value
    	VRequest<Func> *node = new VRequest<Func>( action );
    	requests.insert( node );
    } // uExecutor::send

    // template <typename Return, typename Func> void submit( Future_ISM<Return> &result, Func action ) { // asynchronous call, return value (future)
    // 	FRequest<Return, Func> *node = new FRequest<Return, Func>( action );
    // 	result = node->result;				// race, copy before insert
    // 	requests.insert( node );
    // } // uExecutor::submit

    // Future type is the return type of the action routine, so action is pseudo called to obtain its type in decltype.
    template <typename Func> auto sendrecv( Func action ) -> Future_ISM<decltype(action())> { // asynchronous call, return value (future)
	FRequest<decltype(action()), Func> *node = new FRequest<decltype(action()), Func>( action );
	Future_ISM<decltype(action())> result = node->result;	// race, copy before insert
	requests.insert( node );
	return result;
    } // uExecutor::sendrecv
}; // uExecutor


#endif // __U_FUTURE_H__


// Local Variables: //
// compile-command: "make install" //
// End: //
