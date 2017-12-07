#include "P2PController.h"
#include <QThread>
#include "Locker.h"

static SynchroPrimitives::CriticalSection handshake_cs;



/////////////////////////////////////////////////////////////////////////

void SwarmConnector::join_to_swarm_begin() {
  system_call_wrapper_error_t res = CSystemCallWrapper::join_to_p2p_swarm(swarm_hash, swarm_key, "dhcp");
  if (res == SCWE_SUCCESS)
    emit successfully_joined_swarm(swarm_hash);
  emit join_to_swarm_finished();
}

void SwarmConnector::leave_swarm_begin() {
  system_call_wrapper_error_t res = CSystemCallWrapper::leave_p2p_swarm(swarm_hash);
  if (res == SCWE_SUCCESS)
    emit successfully_left_swarm(swarm_hash);
  emit leave_swarm_finished();
}

/////////////////////////////////////////////////////////////////////////
void HandshakeSender::try_to_handshake(const CEnvironment &env, const CHubContainer &cont) {
  static const int MAX_HANDSHAKE_ATTEMPTS = 10;
  system_call_wrapper_error_t err = SCWE_CONTAINER_IS_NOT_READY;

  for (int ac = 0; ac < MAX_HANDSHAKE_ATTEMPTS && err != SCWE_SUCCESS ; ++ac) {
   err = CSystemCallWrapper::check_container_state(env.hash(), cont.rh_ip());
   if (err == SCWE_SUCCESS) {
     err = CSystemCallWrapper::send_handshake
             (CSettingsManager::Instance().ssh_user(), cont.rh_ip(), cont.port());
   }
   if (err == SCWE_SUCCESS) {
     break;
   }
   QThread::currentThread()->sleep(1);
  }

  if (err == SCWE_SUCCESS)
    emit handshake_success(env.id(), cont.id());
  else
    emit handshake_failure(env.id(), cont.id());
}

void HandshakeSender::send_handshakes(){
  SynchroPrimitives::Locker lock(&handshake_cs); // locker for threads

  for (CEnvironment &env : m_envs) {
    for (CHubContainer cont : env.containers()) {
      try_to_handshake(env, cont);
    }
  }
  emit sent_handshakes_succsessfully();
}

void HandshakeSender::start_handshake(){
  m_th->start();
}

HandshakeSender::HandshakeSender(const std::vector<CEnvironment> envs) : m_envs(envs) {
  m_th = new QThread;
  connect(m_th, &QThread::started, this, &HandshakeSender::send_handshakes);
  connect(this, &HandshakeSender::handshake_success, &P2PController::Instance(), &P2PController::handshaked);
  connect(this, &HandshakeSender::handshake_failure, &P2PController::Instance(), &P2PController::handshake_failed);
  connect(this, &HandshakeSender::sent_handshakes_succsessfully, m_th, &QThread::quit);
  connect(m_th, &QThread::finished, this, &HandshakeSender::deleteLater);
  connect(m_th, &QThread::finished, m_th, &QThread::deleteLater);
  moveToThread(m_th);
}

/////////////////////////////////////////////////////////////////////////


P2PController::P2PController() {
  QTimer *join_to_swarm_timer = new QTimer(this);
  connect(&EnvironmentState::Instance(), &EnvironmentState::environemts_state_updated,
          this, &P2PController::update_handshake_status);
  connect(join_to_swarm_timer, &QTimer::timeout,
          this, &P2PController::update_join_swarm_status);
  join_to_swarm_timer->start(1000 * 60 * 4); // 4 minutes
}

/////////////////////////////////////////////////////////////////////////

void P2PController::joined_swarm(QString hash) {
  envs_joined_swarm_hash.insert(hash);
  qInfo() << "Successfully joined the swarm : " << hash;
}

void P2PController::join_swarm(const CEnvironment &env) {
  qInfo() << "Trying to  join the swarm for environment " << env.name()
          << " with hash: " << env.hash() << " and key: " << env.key();
  SwarmConnector *connector = new SwarmConnector(env.hash(), env.key());
  QThread *th = new QThread(this);
  connect(th, &QThread::started, connector, &SwarmConnector::join_to_swarm_begin);
  connect(connector, &SwarmConnector::successfully_joined_swarm, this, &P2PController::joined_swarm);
  connect(connector, &SwarmConnector::join_to_swarm_finished, th, &QThread::quit);
  connect(th, &QThread::finished, connector, &SwarmConnector::deleteLater);
  connect(th, &QThread::finished, th, &QThread::deleteLater);
  connector->moveToThread(th);
  th->start();
}

/////////////////////////////////////////////////////////////////////////

void P2PController::left_swarm(QString hash) {
  envs_joined_swarm_hash.erase(hash);
  qInfo() << "Successfully left the swarm : " << hash;
}

void P2PController::leave_swarm(const CEnvironment &env) {
  qInfo() << "Trying to leave the swarm for environment " << env.name()
          << " with hash : " << env.hash() <<" and key : " << env.key();
  SwarmConnector *connector = new SwarmConnector(env.hash(), env.key());
  QThread *th = new QThread(this);
  connect(th, &QThread::started, connector, &SwarmConnector::leave_swarm_begin);
  connect(connector, &SwarmConnector::successfully_left_swarm, this, &P2PController::left_swarm);
  connect(connector, &SwarmConnector::leave_swarm_finished, th, &QThread::quit);
  connect(th, &QThread::finished, connector, &SwarmConnector::deleteLater);
  connect(th, &QThread::finished, th, &QThread::deleteLater);
  connector->moveToThread(th);
  th->start();
}

/////////////////////////////////////////////////////////////////////////



/////////////////////////////////////////////////////////////////////////

void P2PController::handshaked(QString env_id, QString cont_id) {
  qInfo() << "Successfull handshake with container " << cont_id << " in environment " << env_id;
  successfull_handshakes.insert(std::make_pair(env_id, cont_id));
}

void P2PController::handshake_failed(QString env_id, QString cont_id) {
  qInfo() << "Unsuccessfull handshake with container " << cont_id << " in environment " << env_id;
  successfull_handshakes.erase(std::make_pair(env_id, cont_id));
}

/////////////////////////////////////////////////////////////////////////

bool P2PController::join_swarm_success(QString swarm_hash) {
  return envs_joined_swarm_hash.find(swarm_hash) != envs_joined_swarm_hash.end();
}

bool P2PController::handshake_success(QString env_id, QString cont_id) {
  return successfull_handshakes.find(std::make_pair(env_id, cont_id)) != successfull_handshakes.end();
}

/////////////////////////////////////////////////////////////////////////

std::vector<CEnvironment> P2PController::get_joined_envs(){
  std::vector<CEnvironment> current_envs = EnvironmentState::Instance().last_updated_envs();
  std::vector<CEnvironment> ret_envs;
  for (CEnvironment &env : current_envs) {
    if (join_swarm_success(env.hash())) {
      ret_envs.push_back(env);
    }
  }
  return ret_envs;
}

/////////////////////////////////////////////////////////////////////////

void P2PController::update_leave_swarm_status(){
  std::vector<CEnvironment> disconnected_envs = EnvironmentState::Instance().disconnected_envs();
  for (CEnvironment &env : disconnected_envs) {
    if (join_swarm_success(env.hash()))
      leave_swarm(env);
  }
}

void P2PController::update_join_swarm_status(){
  std::vector<CEnvironment> current_envs = EnvironmentState::Instance().last_updated_envs(); // last_updated_envs stores both envs that are freshly builded and not old not deleted envs
  for (CEnvironment &env : current_envs) {
    if (!join_swarm_success(env.hash()))
      join_swarm(env);
  }
}
/////////////////////////////////////////////////////////////////////////

void P2PController::update_handshake_status() {
  if (!EnvironmentState::Instance().connected_envs().empty()) {
    update_join_swarm_status();
  }

  if (!EnvironmentState::Instance().disconnected_envs().empty()) {
    update_leave_swarm_status();
  }

  HandshakeSender *handshake_sender = new HandshakeSender(get_joined_envs());
  handshake_sender->start_handshake();
}

/////////////////////////////////////////////////////////////////////////
