#include <resip/stack/SipStack.hxx>
#include <iostream>
#include "../include/SipMessage.hxx"

using namespace resip;

int main()
{
    resip::Log::initialize(resip::Log::Cout, resip::Log::Err, "");
    
    SipStack sipStack;
    sipStack.addTransport(UDP, 5060, V4);
    std::cout << "Server started on UDP/5060 ...\n";
    
    WatchSIPMethods(sipStack);
    
    return 0;
}