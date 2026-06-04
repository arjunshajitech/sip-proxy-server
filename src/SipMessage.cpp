#include <iostream>
#include <resip/stack/Helper.hxx>
#include <resip/stack/NameAddr.hxx>
#include <resip/stack/SipMessage.hxx>
#include <resip/stack/SipStack.hxx>
#include <resip/stack/Uri.hxx>
#include <resip/stack/Via.hxx>
#include <unordered_map>

using namespace resip;
using namespace std;

std::unordered_map<std::string, std::string> gRegistrations;
std::unordered_map<std::string, SipMessage *> gActiveCalls;

void handleRegister(SipStack &stack, SipMessage *msg);
void handleInvite(SipStack &stack, SipMessage *msg);
void handleAck(SipStack &stack, SipMessage *msg);
void handleBye(SipStack &stack, SipMessage *msg);
void handleResponse(SipStack &stack, SipMessage *msg);
void handleDefault(SipStack &stack, SipMessage *msg);

static const char *PROXY_HOST = "192.168.7.135";
static const int PROXY_PORT = 5060;
static const char *PROXY_TRANSPORT = "udp";

static void applyProxyRoute(SipMessage *msg) {
  if (msg->exists(h_Routes))
    msg->remove(h_Routes);

  NameAddr rr;
  rr.uri().scheme() = "sip";
  rr.uri().host() = PROXY_HOST;
  rr.uri().port() = PROXY_PORT;
  rr.uri().param(p_transport) = PROXY_TRANSPORT;
  rr.uri().param(p_lr) = "";
  msg->header(h_RecordRoutes).push_front(rr);
}

static void addProxyVia(SipMessage *msg) {
  Via proxyVia;
  proxyVia.transport() = "UDP";
  proxyVia.sentHost() = PROXY_HOST;
  proxyVia.sentPort() = PROXY_PORT;
  proxyVia.param(p_branch).reset(Helper::computeUniqueBranch());
  msg->header(h_Vias).push_front(proxyVia);
}

static void stripProxyVia(SipMessage *msg) {
  if (!msg->header(h_Vias).empty())
    msg->header(h_Vias).pop_front();
}

void WatchSIPMethods(SipStack &stack) {
  while (true) {
    stack.process(50);

    SipMessage *msg = stack.receive();
    if (!msg)
      continue;

    if (msg->isRequest()) {
      switch (msg->header(h_RequestLine).getMethod()) {
      case REGISTER:
        handleRegister(stack, msg);
        break;
      case INVITE:
        handleInvite(stack, msg);
        break;
      case ACK:
        handleAck(stack, msg);
        break;
      case BYE:
        handleBye(stack, msg);
        break;
      default:
        handleDefault(stack, msg);
        break;
      }
      delete msg;
    } else if (msg->isResponse()) {
      handleResponse(stack, msg);
      delete msg;
    }
  }
}

void handleRegister(SipStack &stack, SipMessage *msg) {
  int expires = 3600;
  if (msg->exists(h_Expires))
    expires = msg->header(h_Expires).value();

  std::string aor = msg->header(h_From).uri().getAor().c_str();

  if (expires == 0) {
    gRegistrations.erase(aor);
    std::cout << "[REGISTER] Unregistered: " << aor << "\n";
  } else if (msg->exists(h_Contacts) && !msg->header(h_Contacts).empty()) {
    std::string contact =
        msg->header(h_Contacts).front().uri().toString().c_str();
    gRegistrations[aor] = contact;
    std::cout << "[REGISTER] " << aor << " -> " << contact << "\n";
  }

  SipMessage *ok = Helper::makeResponse(*msg, 200);
  if (msg->exists(h_Contacts))
    ok->header(h_Contacts) = msg->header(h_Contacts);
  ok->header(h_Expires).value() = expires;
  stack.send(*ok);
  delete ok;
}

void handleInvite(SipStack &stack, SipMessage *msg) {
  std::string callId = msg->header(h_CallID).value().c_str();
  std::string from = msg->header(h_From).uri().getAor().c_str();
  std::string to = msg->header(h_To).uri().getAor().c_str();

  std::cout << "[INVITE] " << from << " -> " << to << "  (Call-ID: " << callId
            << ")\n\n";
  std::cout << *msg << "\n";

  if (msg->exists(h_Contacts) && !msg->header(h_Contacts).empty()) {
    std::string callerContact =
        msg->header(h_Contacts).front().uri().toString().c_str();
    gRegistrations[from] = callerContact;
    std::cout << "[INVITE] Auto-registered caller: " << from << " -> "
              << callerContact << "\n";
  }

  auto it = gRegistrations.find(to);
  if (it == gRegistrations.end()) {
    std::cout << "[INVITE] User not registered: " << to << "\n\n";
    SipMessage *notFound = Helper::makeResponse(*msg, 404);
    stack.send(*notFound);
    delete notFound;
    return;
  }

  std::string contactUri = it->second;
  std::cout << "[INVITE] Forwarding to " << contactUri << "\n\n";

  auto existing = gActiveCalls.find(callId);
  if (existing != gActiveCalls.end()) {
    delete existing->second;
    gActiveCalls.erase(existing);
  }
  gActiveCalls[callId] = new SipMessage(*msg);

  SipMessage *outgoing = new SipMessage(*msg);
  outgoing->header(h_RequestLine).uri() = Uri(contactUri.c_str());
  applyProxyRoute(outgoing);
  addProxyVia(outgoing);

  std::cout << *outgoing << "\n";
  stack.send(*outgoing);
  delete outgoing;
}

void handleAck(SipStack &stack, SipMessage *msg) {
  std::string callId = msg->header(h_CallID).value().c_str();
  std::string from = msg->header(h_From).uri().getAor().c_str();
  std::cout << "[ACK] Call-ID: " << callId << "\n";

  auto it = gActiveCalls.find(callId);
  if (it == gActiveCalls.end()) {
    std::cout << "[ACK] No active call found\n";
    return;
  }

  std::string to = it->second->header(h_To).uri().getAor().c_str();
  auto reg = gRegistrations.find(to);
  if (reg == gRegistrations.end()) {
    std::cout << "[ACK] Callee not registered\n";
    return;
  }

  SipMessage *outgoing = new SipMessage(*msg);
  outgoing->header(h_RequestLine).uri() = Uri(reg->second.c_str());
  applyProxyRoute(outgoing);

  std::cout << "[ACK] Forward to " << to << " from " << from << " \n";
  std::cout << "\n";
  std::cout << *outgoing << "\n";
  stack.send(*outgoing);
  delete outgoing;
}

void handleBye(SipStack &stack, SipMessage *msg) {
  std::string callId = msg->header(h_CallID).value().c_str();
  std::string from = msg->header(h_From).uri().getAor().c_str();
  std::cout << "[BYE] Call-ID: " << callId << " from " << from << "\n";

  auto it = gActiveCalls.find(callId);
  if (it == gActiveCalls.end()) {
    std::cout << "[BYE] No active call found\n";
    SipMessage *ok = Helper::makeResponse(*msg, 200);
    stack.send(*ok);
    delete ok;
    return;
  }

  SipMessage *originalInvite = it->second;
  std::string inviteFrom =
      originalInvite->header(h_From).uri().getAor().c_str();
  std::string inviteTo = originalInvite->header(h_To).uri().getAor().c_str();
  std::string targetAor = (from == inviteFrom) ? inviteTo : inviteFrom;

  auto reg = gRegistrations.find(targetAor);
  if (reg == gRegistrations.end()) {
    std::cout << "[BYE] Target not registered " << targetAor << "\n";
    SipMessage *ok = Helper::makeResponse(*msg, 200);
    stack.send(*ok);
    delete ok;
    return;
  }

  SipMessage *outgoing = new SipMessage(*msg);
  outgoing->header(h_RequestLine).uri() = Uri(reg->second.c_str());

  if (outgoing->exists(h_Routes))
    outgoing->remove(h_Routes);

  if (!outgoing->header(h_Vias).empty())
    outgoing->header(h_Vias).pop_front();
  addProxyVia(outgoing);

  std::cout << "[BYE] Forwarded to " << targetAor << " from " << from << "\n";
  std::cout << "\n";
  std::cout << *outgoing << "\n";
  stack.send(*outgoing);
  delete outgoing;

  SipMessage *ok = Helper::makeResponse(*msg, 200);
  stack.send(*ok);
  delete ok;

  delete it->second;
  gActiveCalls.erase(it);
}

void handleResponse(SipStack &stack, SipMessage *msg) {
  int status = msg->header(h_StatusLine).statusCode();
  std::string callId = msg->header(h_CallID).value().c_str();

  std::cout << "[RESPONSE] " << status << " "
            << msg->header(h_StatusLine).reason().c_str()
            << "  Call-ID: " << callId << "\n";

  if (msg->header(h_CSeq).method() != INVITE)
    return;

  if (status >= 100 && status <= 200) {

    auto it = gActiveCalls.find(callId);
    if (it != gActiveCalls.end()) {
      SipMessage *outgoing = new SipMessage(*msg);
      stripProxyVia(outgoing);
      
      std::cout << "[RESPONSE] Forwarding " << status << " "
                << msg->header(h_StatusLine).reason().c_str() << "\n";
      std::cout << "\n";
      std::cout << *outgoing << "\n";
      
      stack.send(*outgoing);
      delete outgoing;
    }
  } else if (status == 200) {

    auto it = gActiveCalls.find(callId);
    if (it != gActiveCalls.end()) {
      SipMessage *outgoing = new SipMessage(*msg);
      stripProxyVia(outgoing);

      std::cout << "[RESPONSE] Forwarding 200 OK\n";
      std::cout << "\n";
      std::cout << *outgoing << "\n";

      stack.send(*outgoing);
      delete outgoing;
    }

  } else if (status >= 300) {
    auto it = gActiveCalls.find(callId);
    if (it != gActiveCalls.end()) {
      SipMessage *outgoing = new SipMessage(*msg);
      stripProxyVia(outgoing);

      std::cout << "[RESPONSE] Forwarding " << status << "\n";
      std::cout << "\n";
      stack.send(*outgoing);
      delete outgoing;

      delete it->second;
      gActiveCalls.erase(it);
    }
  }
}

void handleDefault(SipStack &stack, SipMessage *msg) {
  MethodTypes method = msg->header(h_RequestLine).getMethod();
  std::cout << "[UNHANDLED] method=" << getMethodName(method) << "\n";
  SipMessage *resp = Helper::makeResponse(*msg, 501);
  stack.send(*resp);
  delete resp;
}
