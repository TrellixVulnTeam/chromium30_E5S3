// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_connection.h"

#include "base/basictypes.h"
#include "base/bind.h"
#include "net/base/net_errors.h"
#include "net/quic/congestion_control/receive_algorithm_interface.h"
#include "net/quic/congestion_control/send_algorithm_interface.h"
#include "net/quic/crypto/null_encrypter.h"
#include "net/quic/crypto/quic_decrypter.h"
#include "net/quic/crypto/quic_encrypter.h"
#include "net/quic/crypto/quic_random.h"
#include "net/quic/quic_protocol.h"
#include "net/quic/quic_utils.h"
#include "net/quic/test_tools/mock_clock.h"
#include "net/quic/test_tools/mock_random.h"
#include "net/quic/test_tools/quic_connection_peer.h"
#include "net/quic/test_tools/quic_framer_peer.h"
#include "net/quic/test_tools/quic_packet_creator_peer.h"
#include "net/quic/test_tools/quic_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::StringPiece;
using std::map;
using std::vector;
using testing::_;
using testing::AnyNumber;
using testing::Between;
using testing::ContainerEq;
using testing::DoAll;
using testing::InSequence;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::StrictMock;
using testing::SaveArg;

namespace net {
namespace test {
namespace {

const char data1[] = "foo";
const char data2[] = "bar";

const bool kFin = true;
const bool kEntropyFlag = true;

const QuicPacketEntropyHash kTestEntropyHash = 76;

class TestReceiveAlgorithm : public ReceiveAlgorithmInterface {
 public:
  explicit TestReceiveAlgorithm(QuicCongestionFeedbackFrame* feedback)
      : feedback_(feedback) {
  }

  bool GenerateCongestionFeedback(
      QuicCongestionFeedbackFrame* congestion_feedback) {
    if (feedback_ == NULL) {
      return false;
    }
    *congestion_feedback = *feedback_;
    return true;
  }

  MOCK_METHOD4(RecordIncomingPacket,
               void(QuicByteCount, QuicPacketSequenceNumber, QuicTime, bool));

 private:
  QuicCongestionFeedbackFrame* feedback_;

  DISALLOW_COPY_AND_ASSIGN(TestReceiveAlgorithm);
};

// TaggingEncrypter appends kTagSize bytes of |tag| to the end of each message.
class TaggingEncrypter : public QuicEncrypter {
 public:
  explicit TaggingEncrypter(uint8 tag)
      : tag_(tag) {
  }

  virtual ~TaggingEncrypter() {}

  // QuicEncrypter interface.
  virtual bool SetKey(StringPiece key) OVERRIDE { return true; }
  virtual bool SetNoncePrefix(StringPiece nonce_prefix) OVERRIDE {
    return true;
  }

  virtual bool Encrypt(StringPiece nonce,
                       StringPiece associated_data,
                       StringPiece plaintext,
                       unsigned char* output) OVERRIDE {
    memcpy(output, plaintext.data(), plaintext.size());
    output += plaintext.size();
    memset(output, tag_, kTagSize);
    return true;
  }

  virtual QuicData* EncryptPacket(QuicPacketSequenceNumber sequence_number,
                                  StringPiece associated_data,
                                  StringPiece plaintext) OVERRIDE {
    const size_t len = plaintext.size() + kTagSize;
    uint8* buffer = new uint8[len];
    Encrypt(StringPiece(), associated_data, plaintext, buffer);
    return new QuicData(reinterpret_cast<char*>(buffer), len, true);
  }

  virtual size_t GetKeySize() const OVERRIDE { return 0; }
  virtual size_t GetNoncePrefixSize() const OVERRIDE { return 0; }

  virtual size_t GetMaxPlaintextSize(size_t ciphertext_size) const OVERRIDE {
    return ciphertext_size - kTagSize;
  }

  virtual size_t GetCiphertextSize(size_t plaintext_size) const OVERRIDE {
    return plaintext_size + kTagSize;
  }

  virtual StringPiece GetKey() const OVERRIDE {
    return StringPiece();
  }

  virtual StringPiece GetNoncePrefix() const OVERRIDE {
    return StringPiece();
  }

 private:
  enum {
    kTagSize = 12,
  };

  const uint8 tag_;
};

// TaggingDecrypter ensures that the final kTagSize bytes of the message all
// have the same value and then removes them.
class TaggingDecrypter : public QuicDecrypter {
 public:
  virtual ~TaggingDecrypter() {}

  // QuicDecrypter interface
  virtual bool SetKey(StringPiece key) OVERRIDE { return true; }
  virtual bool SetNoncePrefix(StringPiece nonce_prefix) OVERRIDE {
    return true;
  }

  virtual bool Decrypt(StringPiece nonce,
                       StringPiece associated_data,
                       StringPiece ciphertext,
                       unsigned char* output,
                       size_t* output_length) OVERRIDE {
    if (ciphertext.size() < kTagSize) {
      return false;
    }
    if (!CheckTag(ciphertext, GetTag(ciphertext))) {
      return false;
    }
    *output_length = ciphertext.size() - kTagSize;
    memcpy(output, ciphertext.data(), *output_length);
    return true;
  }

  virtual QuicData* DecryptPacket(QuicPacketSequenceNumber sequence_number,
                                  StringPiece associated_data,
                                  StringPiece ciphertext) OVERRIDE {
    if (ciphertext.size() < kTagSize) {
      return NULL;
    }
    if (!CheckTag(ciphertext, GetTag(ciphertext))) {
      return NULL;
    }
    const size_t len = ciphertext.size() - kTagSize;
    uint8* buf = new uint8[len];
    memcpy(buf, ciphertext.data(), len);
    return new QuicData(reinterpret_cast<char*>(buf), len,
                        true /* owns buffer */);
  }

  virtual StringPiece GetKey() const OVERRIDE { return StringPiece(); }
  virtual StringPiece GetNoncePrefix() const OVERRIDE { return StringPiece(); }

 protected:
  virtual uint8 GetTag(StringPiece ciphertext) {
    return ciphertext.data()[ciphertext.size()-1];
  }

 private:
  enum {
    kTagSize = 12,
  };

  bool CheckTag(StringPiece ciphertext, uint8 tag) {
    for (size_t i = ciphertext.size() - kTagSize; i < ciphertext.size(); i++) {
      if (ciphertext.data()[i] != tag) {
        return false;
      }
    }

    return true;
  }
};

// StringTaggingDecrypter ensures that the final kTagSize bytes of the message
// match the expected value.
class StrictTaggingDecrypter : public TaggingDecrypter {
 public:
  explicit StrictTaggingDecrypter(uint8 tag) : tag_(tag) {}
  virtual ~StrictTaggingDecrypter() {}

  // TaggingQuicDecrypter
  virtual uint8 GetTag(StringPiece ciphertext) OVERRIDE {
    return tag_;
  }

 private:
  const uint8 tag_;
};

class TestConnectionHelper : public QuicConnectionHelperInterface {
 public:
  TestConnectionHelper(MockClock* clock, MockRandom* random_generator)
      : clock_(clock),
        random_generator_(random_generator),
        retransmission_alarm_(QuicTime::Zero()),
        send_alarm_(QuicTime::Zero().Subtract(
            QuicTime::Delta::FromMilliseconds(1))),
        timeout_alarm_(QuicTime::Zero()),
        blocked_(false),
        is_server_(true),
        use_tagging_decrypter_(false),
        packets_write_attempts_(0) {
  }

  // QuicConnectionHelperInterface
  virtual void SetConnection(QuicConnection* connection) OVERRIDE {}

  virtual const QuicClock* GetClock() const OVERRIDE {
    return clock_;
  }

  virtual QuicRandom* GetRandomGenerator() OVERRIDE {
    return random_generator_;
  }

  virtual int WritePacketToWire(const QuicEncryptedPacket& packet,
                                int* error) OVERRIDE {
    ++packets_write_attempts_;

    if (packet.length() >= sizeof(final_bytes_of_last_packet_)) {
      memcpy(&final_bytes_of_last_packet_, packet.data() + packet.length() - 4,
             sizeof(final_bytes_of_last_packet_));
    }

    QuicFramer framer(QuicVersionMax(), QuicTime::Zero(), is_server_);
    if (use_tagging_decrypter_) {
      framer.SetDecrypter(new TaggingDecrypter);
    }
    FramerVisitorCapturingFrames visitor;
    framer.set_visitor(&visitor);
    EXPECT_TRUE(framer.ProcessPacket(packet));
    header_ = *visitor.header();
    frame_count_ = visitor.frame_count();
    if (visitor.ack()) {
      ack_.reset(new QuicAckFrame(*visitor.ack()));
    }
    if (visitor.feedback()) {
      feedback_.reset(new QuicCongestionFeedbackFrame(*visitor.feedback()));
    }
    if (visitor.stream_frames() != NULL && !visitor.stream_frames()->empty()) {
      stream_frames_ = *visitor.stream_frames();
    }
    if (visitor.version_negotiation_packet() != NULL) {
      version_negotiation_packet_.reset(new QuicVersionNegotiationPacket(
          *visitor.version_negotiation_packet()));
    }
    if (blocked_) {
      *error = ERR_IO_PENDING;
      return -1;
    }
    *error = 0;
    last_packet_size_ = packet.length();
    return last_packet_size_;
  }

  virtual bool IsWriteBlockedDataBuffered() OVERRIDE {
    return false;
  }

  virtual bool IsWriteBlocked(int error) OVERRIDE {
    return error == ERR_IO_PENDING;
  }

  virtual void SetRetransmissionAlarm(QuicTime::Delta delay) OVERRIDE {
    retransmission_alarm_ = clock_->ApproximateNow().Add(delay);
  }

  virtual void SetSendAlarm(QuicTime alarm_time) OVERRIDE {
    send_alarm_ = alarm_time;
  }

  virtual void SetTimeoutAlarm(QuicTime::Delta delay) OVERRIDE {
    timeout_alarm_ = clock_->ApproximateNow().Add(delay);
  }

  virtual bool IsSendAlarmSet() OVERRIDE {
    return send_alarm_ >= clock_->ApproximateNow();
  }

  virtual void UnregisterSendAlarmIfRegistered() OVERRIDE {
    send_alarm_ =
        QuicTime::Zero().Subtract(QuicTime::Delta::FromMilliseconds(1));
  }

  virtual void SetAckAlarm(QuicTime::Delta delay) OVERRIDE {}
  virtual void ClearAckAlarm() OVERRIDE {}

  QuicTime retransmission_alarm() const {
    return retransmission_alarm_;
  }

  QuicTime timeout_alarm() const { return timeout_alarm_; }

  QuicPacketHeader* header() { return &header_; }

  size_t frame_count() const { return frame_count_; }

  QuicAckFrame* ack() { return ack_.get(); }

  QuicCongestionFeedbackFrame* feedback() { return feedback_.get(); }

  const vector<QuicStreamFrame>* stream_frames() const {
    return &stream_frames_;
  }

  size_t last_packet_size() {
    return last_packet_size_;
  }

  QuicVersionNegotiationPacket* version_negotiation_packet() {
    return version_negotiation_packet_.get();
  }

  void set_blocked(bool blocked) { blocked_ = blocked; }

  void set_is_server(bool is_server) { is_server_ = is_server; }

  // final_bytes_of_last_packet_ returns the last four bytes of the previous
  // packet as a little-endian, uint32. This is intended to be used with a
  // TaggingEncrypter so that tests can determine which encrypter was used for
  // a given packet.
  uint32 final_bytes_of_last_packet() { return final_bytes_of_last_packet_; }

  void use_tagging_decrypter() {
    use_tagging_decrypter_ = true;
  }

  uint32 packets_write_attempts() { return packets_write_attempts_; }

 private:
  MockClock* clock_;
  MockRandom* random_generator_;
  QuicTime retransmission_alarm_;
  QuicTime send_alarm_;
  QuicTime timeout_alarm_;
  QuicPacketHeader header_;
  size_t frame_count_;
  scoped_ptr<QuicAckFrame> ack_;
  scoped_ptr<QuicCongestionFeedbackFrame> feedback_;
  vector<QuicStreamFrame> stream_frames_;
  scoped_ptr<QuicVersionNegotiationPacket> version_negotiation_packet_;
  size_t last_packet_size_;
  bool blocked_;
  bool is_server_;
  uint32 final_bytes_of_last_packet_;
  bool use_tagging_decrypter_;
  uint32 packets_write_attempts_;

  DISALLOW_COPY_AND_ASSIGN(TestConnectionHelper);
};

class TestConnection : public QuicConnection {
 public:
  TestConnection(QuicGuid guid,
                 IPEndPoint address,
                 TestConnectionHelper* helper,
                 bool is_server)
      : QuicConnection(guid, address, helper, is_server, QuicVersionMax()),
        helper_(helper) {
    helper_->set_is_server(!is_server);
  }

  void SendAck() {
    QuicConnectionPeer::SendAck(this);
  }

  void SetReceiveAlgorithm(TestReceiveAlgorithm* receive_algorithm) {
     QuicConnectionPeer::SetReceiveAlgorithm(this, receive_algorithm);
  }

  void SetSendAlgorithm(SendAlgorithmInterface* send_algorithm) {
    QuicConnectionPeer::SetSendAlgorithm(this, send_algorithm);
  }

  QuicConsumedData SendStreamData1() {
    return SendStreamData(1u, "food", 0, !kFin);
  }

  QuicConsumedData SendStreamData2() {
    return SendStreamData(2u, "food2", 0, !kFin);
  }

  bool is_server() {
    return QuicConnectionPeer::IsServer(this);
  }

  void set_version(QuicVersion version) {
    framer_.set_version(version);
  }

  void set_is_server(bool is_server) {
    helper_->set_is_server(!is_server);
    QuicPacketCreatorPeer::SetIsServer(
        QuicConnectionPeer::GetPacketCreator(this), is_server);
    QuicConnectionPeer::SetIsServer(this, is_server);
  }

  using QuicConnection::SendOrQueuePacket;
  using QuicConnection::DontWaitForPacketsBefore;
  using QuicConnection::SelectMutualVersion;

 private:
  TestConnectionHelper* helper_;

  DISALLOW_COPY_AND_ASSIGN(TestConnection);
};

class QuicConnectionTest : public ::testing::Test {
 protected:
  QuicConnectionTest()
      : guid_(42),
        framer_(QuicVersionMax(), QuicTime::Zero(), false),
        creator_(guid_, &framer_, QuicRandom::GetInstance(), false),
        send_algorithm_(new StrictMock<MockSendAlgorithm>),
        helper_(new TestConnectionHelper(&clock_, &random_generator_)),
        connection_(guid_, IPEndPoint(), helper_, false),
        frame1_(1, false, 0, data1),
        frame2_(1, false, 3, data2),
        accept_packet_(true) {
    connection_.set_visitor(&visitor_);
    connection_.SetSendAlgorithm(send_algorithm_);
    // Simplify tests by not sending feedback unless specifically configured.
    SetFeedback(NULL);
    EXPECT_CALL(
        *send_algorithm_, TimeUntilSend(_, _, _, _)).WillRepeatedly(Return(
            QuicTime::Delta::Zero()));
    EXPECT_CALL(*receive_algorithm_,
                RecordIncomingPacket(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(*send_algorithm_, RetransmissionDelay()).WillRepeatedly(
        Return(QuicTime::Delta::Zero()));
  }

  QuicAckFrame* outgoing_ack() {
    return QuicConnectionPeer::GetOutgoingAck(&connection_);
  }

  QuicAckFrame* last_ack() {
    return helper_->ack();
  }

  QuicCongestionFeedbackFrame* last_feedback() {
    return helper_->feedback();
  }

  QuicPacketHeader* last_header() {
    return helper_->header();
  }

  size_t last_sent_packet_size() {
    return helper_->last_packet_size();
  }

  uint32 final_bytes_of_last_packet() {
    return helper_->final_bytes_of_last_packet();
  }

  void use_tagging_decrypter() {
    helper_->use_tagging_decrypter();
  }

  void ProcessPacket(QuicPacketSequenceNumber number) {
    EXPECT_CALL(visitor_, OnPacket(_, _, _, _))
        .WillOnce(Return(accept_packet_));
    ProcessDataPacket(number, 0, !kEntropyFlag);
  }

  QuicPacketEntropyHash ProcessFramePacket(QuicFrame frame) {
    QuicFrames frames;
    frames.push_back(QuicFrame(frame));
    QuicPacketCreatorPeer::SetSendVersionInPacket(&creator_,
                                                  connection_.is_server());
    SerializedPacket serialized_packet = creator_.SerializeAllFrames(frames);
    scoped_ptr<QuicPacket> packet(serialized_packet.packet);
    scoped_ptr<QuicEncryptedPacket> encrypted(
        framer_.EncryptPacket(ENCRYPTION_NONE,
                              serialized_packet.sequence_number, *packet));
    connection_.ProcessUdpPacket(IPEndPoint(), IPEndPoint(), *encrypted);
    return serialized_packet.entropy_hash;
  }

  size_t ProcessFecProtectedPacket(QuicPacketSequenceNumber number,
                                 bool expect_revival) {
    if (expect_revival) {
      EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).Times(2).WillRepeatedly(
          Return(accept_packet_));
    } else {
      EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).WillOnce(
          Return(accept_packet_));
    }
    return ProcessDataPacket(number, 1, !kEntropyFlag);
  }

  size_t ProcessDataPacket(QuicPacketSequenceNumber number,
                           QuicFecGroupNumber fec_group,
                           bool entropy_flag) {
    return ProcessDataPacketAtLevel(number, fec_group, entropy_flag,
                                    ENCRYPTION_NONE);
  }

  size_t ProcessDataPacketAtLevel(QuicPacketSequenceNumber number,
                                  QuicFecGroupNumber fec_group,
                                  bool entropy_flag,
                                  EncryptionLevel level) {
    scoped_ptr<QuicPacket> packet(ConstructDataPacket(number, fec_group,
                                                      entropy_flag));
    scoped_ptr<QuicEncryptedPacket> encrypted(framer_.EncryptPacket(
        level, number, *packet));
    connection_.ProcessUdpPacket(IPEndPoint(), IPEndPoint(), *encrypted);
    return encrypted->length();
  }

  void ProcessClosePacket(QuicPacketSequenceNumber number,
                          QuicFecGroupNumber fec_group) {
    EXPECT_CALL(visitor_, OnCanWrite()).Times(1).WillOnce(Return(true));
    scoped_ptr<QuicPacket> packet(ConstructClosePacket(number, fec_group));
    scoped_ptr<QuicEncryptedPacket> encrypted(framer_.EncryptPacket(
        ENCRYPTION_NONE, number, *packet));
    connection_.ProcessUdpPacket(IPEndPoint(), IPEndPoint(), *encrypted);
  }

  size_t ProcessFecProtectedPacket(QuicPacketSequenceNumber number,
                                 bool expect_revival, bool entropy_flag) {
    if (expect_revival) {
      EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).WillOnce(DoAll(
          SaveArg<2>(&revived_header_), Return(accept_packet_)));
    }
    EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).WillOnce(Return(accept_packet_))
        .RetiresOnSaturation();
    return ProcessDataPacket(number, 1, entropy_flag);
  }

  // Sends an FEC packet that covers the packets that would have been sent.
  size_t ProcessFecPacket(QuicPacketSequenceNumber number,
                          QuicPacketSequenceNumber min_protected_packet,
                          bool expect_revival,
                          bool entropy_flag) {
    if (expect_revival) {
      EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).WillOnce(DoAll(
          SaveArg<2>(&revived_header_), Return(accept_packet_)));
    }

    // Construct the decrypted data packet so we can compute the correct
    // redundancy.
    scoped_ptr<QuicPacket> data_packet(ConstructDataPacket(number, 1,
                                                           !kEntropyFlag));

    header_.public_header.guid = guid_;
    header_.public_header.reset_flag = false;
    header_.public_header.version_flag = false;
    header_.entropy_flag = entropy_flag;
    header_.fec_flag = true;
    header_.packet_sequence_number = number;
    header_.is_in_fec_group = IN_FEC_GROUP;
    header_.fec_group = min_protected_packet;
    QuicFecData fec_data;
    fec_data.fec_group = header_.fec_group;
    // Since all data packets in this test have the same payload, the
    // redundancy is either equal to that payload or the xor of that payload
    // with itself, depending on the number of packets.
    if (((number - min_protected_packet) % 2) == 0) {
      for (size_t i = GetStartOfFecProtectedData(
               header_.public_header.guid_length,
               header_.public_header.version_flag,
               header_.public_header.sequence_number_length);
           i < data_packet->length(); ++i) {
        data_packet->mutable_data()[i] ^= data_packet->data()[i];
      }
    }
    fec_data.redundancy = data_packet->FecProtectedData();
    scoped_ptr<QuicPacket> fec_packet(
        framer_.ConstructFecPacket(header_, fec_data).packet);
    scoped_ptr<QuicEncryptedPacket> encrypted(
        framer_.EncryptPacket(ENCRYPTION_NONE, number, *fec_packet));

    connection_.ProcessUdpPacket(IPEndPoint(), IPEndPoint(), *encrypted);
    return encrypted->length();
  }

  QuicByteCount SendStreamDataToPeer(QuicStreamId id, StringPiece data,
                                     QuicStreamOffset offset, bool fin,
                                     QuicPacketSequenceNumber* last_packet) {
    QuicByteCount packet_size;
    EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).WillOnce(
        SaveArg<2>(&packet_size));
    connection_.SendStreamData(id, data, offset, fin);
    if (last_packet != NULL) {
      *last_packet =
          QuicConnectionPeer::GetPacketCreator(&connection_)->sequence_number();
    }
    EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(AnyNumber());
    return packet_size;
  }

  void SendAckPacketToPeer() {
    EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(1);
    connection_.SendAck();
    EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(AnyNumber());
  }

  QuicPacketEntropyHash ProcessAckPacket(QuicAckFrame* frame,
                                         bool expect_writes) {
    if (expect_writes) {
      EXPECT_CALL(visitor_, OnCanWrite()).Times(1).WillOnce(Return(true));
    }
    return ProcessFramePacket(QuicFrame(frame));
  }

  QuicPacketEntropyHash ProcessGoAwayPacket(QuicGoAwayFrame* frame) {
    return ProcessFramePacket(QuicFrame(frame));
  }

  bool IsMissing(QuicPacketSequenceNumber number) {
    return IsAwaitingPacket(outgoing_ack()->received_info, number);
  }

  QuicPacket* ConstructDataPacket(QuicPacketSequenceNumber number,
                                  QuicFecGroupNumber fec_group,
                                  bool entropy_flag) {
    header_.public_header.guid = guid_;
    header_.public_header.reset_flag = false;
    header_.public_header.version_flag = false;
    header_.entropy_flag = entropy_flag;
    header_.fec_flag = false;
    header_.packet_sequence_number = number;
    header_.is_in_fec_group = fec_group == 0u ? NOT_IN_FEC_GROUP : IN_FEC_GROUP;
    header_.fec_group = fec_group;

    QuicFrames frames;
    QuicFrame frame(&frame1_);
    frames.push_back(frame);
    QuicPacket* packet =
        framer_.ConstructFrameDataPacket(header_, frames).packet;
    EXPECT_TRUE(packet != NULL);
    return packet;
  }

  QuicPacket* ConstructClosePacket(QuicPacketSequenceNumber number,
                                   QuicFecGroupNumber fec_group) {
    header_.public_header.guid = guid_;
    header_.packet_sequence_number = number;
    header_.public_header.reset_flag = false;
    header_.public_header.version_flag = false;
    header_.entropy_flag = false;
    header_.fec_flag = false;
    header_.is_in_fec_group = fec_group == 0u ? NOT_IN_FEC_GROUP : IN_FEC_GROUP;
    header_.fec_group = fec_group;

    QuicConnectionCloseFrame qccf;
    qccf.error_code = QUIC_PEER_GOING_AWAY;
    qccf.ack_frame = QuicAckFrame(0, QuicTime::Zero(), 1);

    QuicFrames frames;
    QuicFrame frame(&qccf);
    frames.push_back(frame);
    QuicPacket* packet =
        framer_.ConstructFrameDataPacket(header_, frames).packet;
    EXPECT_TRUE(packet != NULL);
    return packet;
  }

  void SetFeedback(QuicCongestionFeedbackFrame* feedback) {
    receive_algorithm_ = new TestReceiveAlgorithm(feedback);
    connection_.SetReceiveAlgorithm(receive_algorithm_);
  }

  QuicGuid guid_;
  QuicFramer framer_;
  QuicPacketCreator creator_;

  MockSendAlgorithm* send_algorithm_;
  TestReceiveAlgorithm* receive_algorithm_;
  MockClock clock_;
  MockRandom random_generator_;
  TestConnectionHelper* helper_;
  TestConnection connection_;
  testing::StrictMock<MockConnectionVisitor> visitor_;

  QuicPacketHeader header_;
  QuicPacketHeader revived_header_;
  QuicStreamFrame frame1_;
  QuicStreamFrame frame2_;
  bool accept_packet_;

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicConnectionTest);
};

TEST_F(QuicConnectionTest, PacketsInOrder) {
  ProcessPacket(1);
  EXPECT_EQ(1u, outgoing_ack()->received_info.largest_observed);
  EXPECT_EQ(0u, outgoing_ack()->received_info.missing_packets.size());

  ProcessPacket(2);
  EXPECT_EQ(2u, outgoing_ack()->received_info.largest_observed);
  EXPECT_EQ(0u, outgoing_ack()->received_info.missing_packets.size());

  ProcessPacket(3);
  EXPECT_EQ(3u, outgoing_ack()->received_info.largest_observed);
  EXPECT_EQ(0u, outgoing_ack()->received_info.missing_packets.size());
}

TEST_F(QuicConnectionTest, PacketsRejected) {
  ProcessPacket(1);
  EXPECT_EQ(1u, outgoing_ack()->received_info.largest_observed);
  EXPECT_EQ(0u, outgoing_ack()->received_info.missing_packets.size());

  accept_packet_ = false;
  ProcessPacket(2);
  // We should not have an ack for two.
  EXPECT_EQ(1u, outgoing_ack()->received_info.largest_observed);
  EXPECT_EQ(0u, outgoing_ack()->received_info.missing_packets.size());
}

TEST_F(QuicConnectionTest, PacketsOutOfOrder) {
  ProcessPacket(3);
  EXPECT_EQ(3u, outgoing_ack()->received_info.largest_observed);
  EXPECT_TRUE(IsMissing(2));
  EXPECT_TRUE(IsMissing(1));

  ProcessPacket(2);
  EXPECT_EQ(3u, outgoing_ack()->received_info.largest_observed);
  EXPECT_FALSE(IsMissing(2));
  EXPECT_TRUE(IsMissing(1));

  ProcessPacket(1);
  EXPECT_EQ(3u, outgoing_ack()->received_info.largest_observed);
  EXPECT_FALSE(IsMissing(2));
  EXPECT_FALSE(IsMissing(1));
}

TEST_F(QuicConnectionTest, DuplicatePacket) {
  ProcessPacket(3);
  EXPECT_EQ(3u, outgoing_ack()->received_info.largest_observed);
  EXPECT_TRUE(IsMissing(2));
  EXPECT_TRUE(IsMissing(1));

  // Send packet 3 again, but do not set the expectation that
  // the visitor OnPacket() will be called.
  ProcessDataPacket(3, 0, !kEntropyFlag);
  EXPECT_EQ(3u, outgoing_ack()->received_info.largest_observed);
  EXPECT_TRUE(IsMissing(2));
  EXPECT_TRUE(IsMissing(1));
}

TEST_F(QuicConnectionTest, PacketsOutOfOrderWithAdditionsAndLeastAwaiting) {
  ProcessPacket(3);
  EXPECT_EQ(3u, outgoing_ack()->received_info.largest_observed);
  EXPECT_TRUE(IsMissing(2));
  EXPECT_TRUE(IsMissing(1));

  ProcessPacket(2);
  EXPECT_EQ(3u, outgoing_ack()->received_info.largest_observed);
  EXPECT_TRUE(IsMissing(1));

  ProcessPacket(5);
  EXPECT_EQ(5u, outgoing_ack()->received_info.largest_observed);
  EXPECT_TRUE(IsMissing(1));
  EXPECT_TRUE(IsMissing(4));

  // Pretend at this point the client has gotten acks for 2 and 3 and 1 is a
  // packet the peer will not retransmit.  It indicates this by sending 'least
  // awaiting' is 4.  The connection should then realize 1 will not be
  // retransmitted, and will remove it from the missing list.
  creator_.set_sequence_number(5);
  QuicAckFrame frame(0, QuicTime::Zero(), 4);
  ProcessAckPacket(&frame, true);

  // Force an ack to be sent.
  SendAckPacketToPeer();
  EXPECT_TRUE(IsMissing(4));
}

TEST_F(QuicConnectionTest, RejectPacketTooFarOut) {
  // Call ProcessDataPacket rather than ProcessPacket, as we should not get a
  // packet call to the visitor.
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_INVALID_PACKET_HEADER, false));
  ProcessDataPacket(6000, 0, !kEntropyFlag);
}

TEST_F(QuicConnectionTest, TruncatedAck) {
  EXPECT_CALL(visitor_, OnAck(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*send_algorithm_, OnIncomingAck(_, _, _)).Times(2);
  EXPECT_CALL(*send_algorithm_, OnIncomingLoss(_)).Times(1);
  for (int i = 0; i < 200; ++i) {
    SendStreamDataToPeer(1, "foo", i * 3, !kFin, NULL);
  }

  QuicAckFrame frame(0, QuicTime::Zero(), 1);
  frame.received_info.largest_observed = 192;
  InsertMissingPacketsBetween(&frame.received_info, 1, 192);
  frame.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 192) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 191);

  ProcessAckPacket(&frame, true);

  EXPECT_TRUE(QuicConnectionPeer::GetReceivedTruncatedAck(&connection_));

  frame.received_info.missing_packets.erase(191);
  frame.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 192) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 190);

  ProcessAckPacket(&frame, true);
  EXPECT_FALSE(QuicConnectionPeer::GetReceivedTruncatedAck(&connection_));
}

TEST_F(QuicConnectionTest, DISABLED_AckReceiptCausesAckSend) {
  ProcessPacket(1);
  // Delay sending, then queue up an ack.
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::FromMicroseconds(1)));
  QuicConnectionPeer::SendAck(&connection_);

  // Process an ack with a least unacked of the received ack.
  // This causes an ack to be sent when TimeUntilSend returns 0.
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillRepeatedly(
                  testing::Return(QuicTime::Delta::Zero()));
  // Skip a packet and then record an ack.
  creator_.set_sequence_number(2);
  QuicAckFrame frame(0, QuicTime::Zero(), 3);
  ProcessAckPacket(&frame, true);
}

TEST_F(QuicConnectionTest, LeastUnackedLower) {
  SendStreamDataToPeer(1, "foo", 0, !kFin, NULL);
  SendStreamDataToPeer(1, "bar", 3, !kFin, NULL);
  SendStreamDataToPeer(1, "eep", 6, !kFin, NULL);

  // Start out saying the least unacked is 2
  creator_.set_sequence_number(5);
  QuicAckFrame frame(0, QuicTime::Zero(), 2);
  ProcessAckPacket(&frame, true);

  // Change it to 1, but lower the sequence number to fake out-of-order packets.
  // This should be fine.
  creator_.set_sequence_number(1);
  QuicAckFrame frame2(0, QuicTime::Zero(), 1);
  // The scheduler will not process out of order acks.
  ProcessAckPacket(&frame2, false);

  // Now claim it's one, but set the ordering so it was sent "after" the first
  // one.  This should cause a connection error.
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_INVALID_ACK_DATA, false));
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  creator_.set_sequence_number(7);
  ProcessAckPacket(&frame2, false);
}

TEST_F(QuicConnectionTest, LargestObservedLower) {
  SendStreamDataToPeer(1, "foo", 0, !kFin, NULL);
  SendStreamDataToPeer(1, "bar", 3, !kFin, NULL);
  SendStreamDataToPeer(1, "eep", 6, !kFin, NULL);
  EXPECT_CALL(*send_algorithm_, OnIncomingAck(_, _, _)).Times(2);

  // Start out saying the largest observed is 2.
  QuicAckFrame frame(2, QuicTime::Zero(), 0);
  frame.received_info.entropy_hash = QuicConnectionPeer::GetSentEntropyHash(
      &connection_, 2);
  EXPECT_CALL(visitor_, OnAck(_));
  ProcessAckPacket(&frame, true);

  // Now change it to 1, and it should cause a connection error.
  QuicAckFrame frame2(1, QuicTime::Zero(), 0);
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_INVALID_ACK_DATA, false));
  ProcessAckPacket(&frame2, false);
}

TEST_F(QuicConnectionTest, LeastUnackedGreaterThanPacketSequenceNumber) {
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_INVALID_ACK_DATA, false));
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  // Create an ack with least_unacked is 2 in packet number 1.
  creator_.set_sequence_number(0);
  QuicAckFrame frame(0, QuicTime::Zero(), 2);
  ProcessAckPacket(&frame, false);
}

TEST_F(QuicConnectionTest,
       NackSequenceNumberGreaterThanLargestReceived) {
  SendStreamDataToPeer(1, "foo", 0, !kFin, NULL);
  SendStreamDataToPeer(1, "bar", 3, !kFin, NULL);
  SendStreamDataToPeer(1, "eep", 6, !kFin, NULL);

  EXPECT_CALL(visitor_, ConnectionClose(QUIC_INVALID_ACK_DATA, false));
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  QuicAckFrame frame(0, QuicTime::Zero(), 1);
  frame.received_info.missing_packets.insert(3);
  ProcessAckPacket(&frame, false);
}

TEST_F(QuicConnectionTest, AckUnsentData) {
  // Ack a packet which has not been sent.
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_INVALID_ACK_DATA, false));
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  QuicAckFrame frame(1, QuicTime::Zero(), 0);
  ProcessAckPacket(&frame, false);
}

TEST_F(QuicConnectionTest, AckAll) {
  ProcessPacket(1);

  creator_.set_sequence_number(1);
  QuicAckFrame frame1(0, QuicTime::Zero(), 1);
  ProcessAckPacket(&frame1, true);
}

TEST_F(QuicConnectionTest, DontWaitForPacketsBefore) {
  ProcessPacket(2);
  ProcessPacket(7);
  EXPECT_TRUE(connection_.DontWaitForPacketsBefore(4));
  EXPECT_EQ(3u, outgoing_ack()->received_info.missing_packets.size());
}

TEST_F(QuicConnectionTest, BasicSending) {
  EXPECT_CALL(*send_algorithm_, OnIncomingAck(_, _, _)).Times(6);
  QuicPacketSequenceNumber last_packet;
  SendStreamDataToPeer(1, "foo", 0, !kFin, &last_packet);  // Packet 1
  EXPECT_EQ(1u, last_packet);
  SendAckPacketToPeer();  // Packet 2

  EXPECT_EQ(1u, last_ack()->sent_info.least_unacked);

  SendAckPacketToPeer();  // Packet 3
  EXPECT_EQ(1u, last_ack()->sent_info.least_unacked);

  SendStreamDataToPeer(1u, "bar", 3, !kFin, &last_packet);  // Packet 4
  EXPECT_EQ(4u, last_packet);
  SendAckPacketToPeer();  // Packet 5
  EXPECT_EQ(1u, last_ack()->sent_info.least_unacked);

  SequenceNumberSet expected_acks;
  expected_acks.insert(1);

  // Client acks up to packet 3
  EXPECT_CALL(visitor_, OnAck(ContainerEq(expected_acks)));
  QuicAckFrame frame(3, QuicTime::Zero(), 0);
  frame.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 3);
  ProcessAckPacket(&frame, true);
  SendAckPacketToPeer();  // Packet 6

  // As soon as we've acked one, we skip ack packets 2 and 3 and note lack of
  // ack for 4.
  EXPECT_EQ(4u, last_ack()->sent_info.least_unacked);

  expected_acks.clear();
  expected_acks.insert(4);

  // Client acks up to packet 4, the last packet
  EXPECT_CALL(visitor_, OnAck(ContainerEq(expected_acks)));
  QuicAckFrame frame2(6, QuicTime::Zero(), 0);
  frame2.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 6);
  ProcessAckPacket(&frame2, true);  // Even parity triggers ack packet 7

  // The least packet awaiting ack should now be 7
  EXPECT_EQ(7u, last_ack()->sent_info.least_unacked);

  // If we force an ack, we shouldn't change our retransmit state.
  SendAckPacketToPeer();  // Packet 8
  EXPECT_EQ(8u, last_ack()->sent_info.least_unacked);

  // But if we send more data it should.
  SendStreamDataToPeer(1, "eep", 6, !kFin, &last_packet);  // Packet 9
  EXPECT_EQ(9u, last_packet);
  SendAckPacketToPeer();  // Packet10
  EXPECT_EQ(9u, last_ack()->sent_info.least_unacked);
}

TEST_F(QuicConnectionTest, FECSending) {
  // All packets carry version info till version is negotiated.
  size_t payload_length;
  connection_.options()->max_packet_length =
      GetPacketLengthForOneStream(
          kIncludeVersion, IN_FEC_GROUP, &payload_length);
  // And send FEC every two packets.
  connection_.options()->max_packets_per_fec_group = 2;

  // Send 4 data packets and 2 FEC packets.
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(6);
  // TODO(ianswett): The first stream frame will consume 2 fewer bytes.
  const string payload(payload_length * 4, 'a');
  connection_.SendStreamData(1, payload, 0, !kFin);
  // Expect the FEC group to be closed after SendStreamData.
  EXPECT_FALSE(creator_.ShouldSendFec(true));
}

TEST_F(QuicConnectionTest, FECQueueing) {
  // All packets carry version info till version is negotiated.
  size_t payload_length;
  connection_.options()->max_packet_length =
      GetPacketLengthForOneStream(
          kIncludeVersion, IN_FEC_GROUP, &payload_length);
  // And send FEC every two packets.
  connection_.options()->max_packets_per_fec_group = 2;

  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  helper_->set_blocked(true);
  const string payload(payload_length, 'a');
  connection_.SendStreamData(1, payload, 0, !kFin);
  EXPECT_FALSE(creator_.ShouldSendFec(true));
  // Expect the first data packet and the fec packet to be queued.
  EXPECT_EQ(2u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, AbandonFECFromCongestionWindow) {
  connection_.options()->max_packets_per_fec_group = 1;
  // 1 Data and 1 FEC packet.
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(2);
  connection_.SendStreamData(1, "foo", 0, !kFin);

  // Larger timeout for FEC bytes to expire.
  const QuicTime::Delta retransmission_time =
      QuicTime::Delta::FromMilliseconds(5000);
  clock_.AdvanceTime(retransmission_time);

  // Send only data packet.
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(1);
  // Abandon both FEC and data packet.
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(_, _)).Times(2);

  connection_.OnRetransmissionTimeout();
}

TEST_F(QuicConnectionTest, DontAbandonAckedFEC) {
  connection_.options()->max_packets_per_fec_group = 1;
  const QuicPacketSequenceNumber sequence_number =
      QuicConnectionPeer::GetPacketCreator(&connection_)->sequence_number() + 1;

  // 1 Data and 1 FEC packet.
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(2);
  connection_.SendStreamData(1, "foo", 0, !kFin);

  QuicAckFrame ack_fec(2, QuicTime::Zero(), 1);
  // Data packet missing.
  ack_fec.received_info.missing_packets.insert(1);
  ack_fec.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 2) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 1);

  EXPECT_CALL(visitor_, OnAck(_)).Times(1);
  EXPECT_CALL(*send_algorithm_, OnIncomingAck(_, _, _)).Times(1);
  EXPECT_CALL(*send_algorithm_, OnIncomingLoss(_)).Times(1);

  ProcessAckPacket(&ack_fec, true);

  const QuicTime::Delta kDefaultRetransmissionTime =
      QuicTime::Delta::FromMilliseconds(5000);
  clock_.AdvanceTime(kDefaultRetransmissionTime);

  // Abandon only data packet, FEC has been acked.
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(sequence_number, _)).Times(1);
  // Send only data packet.
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(1);
  connection_.OnRetransmissionTimeout();
}

TEST_F(QuicConnectionTest, FramePacking) {
  // Block the connection.
  helper_->SetSendAlarm(
      clock_.ApproximateNow().Add(QuicTime::Delta::FromSeconds(1)));

  // Send an ack and two stream frames in 1 packet by queueing them.
  connection_.SendAck();
  EXPECT_CALL(visitor_, OnCanWrite()).WillOnce(DoAll(
      IgnoreResult(InvokeWithoutArgs(&connection_,
                                     &TestConnection::SendStreamData1)),
      IgnoreResult(InvokeWithoutArgs(&connection_,
                                     &TestConnection::SendStreamData2)),
      Return(true)));

  // Unblock the connection.
  helper_->UnregisterSendAlarmIfRegistered();
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, _, NOT_RETRANSMISSION))
      .Times(1);
  connection_.OnCanWrite();
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_FALSE(connection_.HasQueuedData());

  // Parse the last packet and ensure it's an ack and two stream frames from
  // two different streams.
  EXPECT_EQ(3u, helper_->frame_count());
  EXPECT_TRUE(helper_->ack());
  EXPECT_EQ(2u, helper_->stream_frames()->size());
  EXPECT_EQ(1u, (*helper_->stream_frames())[0].stream_id);
  EXPECT_EQ(2u, (*helper_->stream_frames())[1].stream_id);
}

TEST_F(QuicConnectionTest, FramePackingFEC) {
  // Enable fec.
  connection_.options()->max_packets_per_fec_group = 6;
  // Block the connection.
  helper_->SetSendAlarm(
      clock_.ApproximateNow().Add(QuicTime::Delta::FromSeconds(1)));

  // Send an ack and two stream frames in 1 packet by queueing them.
  connection_.SendAck();
  EXPECT_CALL(visitor_, OnCanWrite()).WillOnce(DoAll(
      IgnoreResult(InvokeWithoutArgs(&connection_,
                                     &TestConnection::SendStreamData1)),
      IgnoreResult(InvokeWithoutArgs(&connection_,
                                     &TestConnection::SendStreamData2)),
      Return(true)));

  // Unblock the connection.
  helper_->UnregisterSendAlarmIfRegistered();
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, _, NOT_RETRANSMISSION)).Times(2);
  connection_.OnCanWrite();
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_FALSE(connection_.HasQueuedData());

  // Parse the last packet and ensure it's in an fec group.
  EXPECT_EQ(1u, helper_->header()->fec_group);
  EXPECT_EQ(0u, helper_->frame_count());
}

TEST_F(QuicConnectionTest, OnCanWrite) {
  // Visitor's OnCanWill send data, but will return false.
  EXPECT_CALL(visitor_, OnCanWrite()).WillOnce(DoAll(
      IgnoreResult(InvokeWithoutArgs(&connection_,
                                     &TestConnection::SendStreamData1)),
      IgnoreResult(InvokeWithoutArgs(&connection_,
                                     &TestConnection::SendStreamData2)),
      Return(false)));

  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillRepeatedly(
                  testing::Return(QuicTime::Delta::Zero()));

  // Unblock the connection.
  connection_.OnCanWrite();
  // Parse the last packet and ensure it's the two stream frames from
  // two different streams.
  EXPECT_EQ(2u, helper_->frame_count());
  EXPECT_EQ(2u, helper_->stream_frames()->size());
  EXPECT_EQ(1u, (*helper_->stream_frames())[0].stream_id);
  EXPECT_EQ(2u, (*helper_->stream_frames())[1].stream_id);
}

TEST_F(QuicConnectionTest, RetransmitOnNack) {
  EXPECT_CALL(*send_algorithm_, OnIncomingAck(_, _, _)).Times(2);
  EXPECT_CALL(*send_algorithm_, OnIncomingLoss(_)).Times(1);
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(2, _)).Times(1);
  QuicPacketSequenceNumber last_packet;
  QuicByteCount second_packet_size;
  SendStreamDataToPeer(1, "foo", 0, !kFin, &last_packet);  // Packet 1
  second_packet_size =
      SendStreamDataToPeer(1, "foos", 3, !kFin, &last_packet);  // Packet 2
  SendStreamDataToPeer(1, "fooos", 7, !kFin, &last_packet);  // Packet 3

  SequenceNumberSet expected_acks;
  expected_acks.insert(1);
  EXPECT_CALL(visitor_, OnAck(ContainerEq(expected_acks)));

  // Client acks one but not two or three.  Right now we only retransmit on
  // explicit nack, so it should not trigger a retransimission.
  QuicAckFrame ack_one(1, QuicTime::Zero(), 0);
  ack_one.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 1);
  ProcessAckPacket(&ack_one, true);
  ProcessAckPacket(&ack_one, true);
  ProcessAckPacket(&ack_one, true);

  expected_acks.clear();
  expected_acks.insert(3);
  EXPECT_CALL(visitor_, OnAck(ContainerEq(expected_acks)));

  // Client acks up to 3 with two explicitly missing.  Two nacks should cause no
  // change.
  QuicAckFrame nack_two(3, QuicTime::Zero(), 0);
  nack_two.received_info.missing_packets.insert(2);
  nack_two.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 3) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 2) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 1);
  ProcessAckPacket(&nack_two, true);
  ProcessAckPacket(&nack_two, true);

  // The third nack should trigger a retransimission.
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, second_packet_size - kQuicVersionSize,
                         IS_RETRANSMISSION)).Times(1);
  ProcessAckPacket(&nack_two, true);
}

TEST_F(QuicConnectionTest, RetransmitNackedLargestObserved) {
  EXPECT_CALL(*send_algorithm_, OnIncomingLoss(_)).Times(1);
  QuicPacketSequenceNumber largest_observed;
  QuicByteCount packet_size;
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, NOT_RETRANSMISSION))
      .WillOnce(DoAll(SaveArg<1>(&largest_observed), SaveArg<2>(&packet_size)));
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(1, _)).Times(1);
  connection_.SendStreamData(1, "foo", 0, !kFin);
  QuicAckFrame frame(1, QuicTime::Zero(), largest_observed);
  frame.received_info.missing_packets.insert(largest_observed);
  frame.received_info.entropy_hash = QuicConnectionPeer::GetSentEntropyHash(
      &connection_, largest_observed - 1);
  ProcessAckPacket(&frame, true);
  // Second udp packet will force an ack frame.
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, _, NOT_RETRANSMISSION));
  ProcessAckPacket(&frame, true);
  // Third nack should retransmit the largest observed packet.
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, packet_size - kQuicVersionSize,
                                           IS_RETRANSMISSION));
  ProcessAckPacket(&frame, true);
}

TEST_F(QuicConnectionTest, RetransmitNackedPacketsOnTruncatedAck) {
  for (int i = 0; i < 200; ++i) {
    EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(1);
    connection_.SendStreamData(1, "foo", i * 3, !kFin);
  }

  // Make a truncated ack frame.
  QuicAckFrame frame(0, QuicTime::Zero(), 1);
  frame.received_info.largest_observed = 192;
  InsertMissingPacketsBetween(&frame.received_info, 1, 192);
  frame.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 192) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 191);


  EXPECT_CALL(*send_algorithm_, OnIncomingAck(_, _, _)).Times(1);
  EXPECT_CALL(*send_algorithm_, OnIncomingLoss(_)).Times(1);
  EXPECT_CALL(visitor_, OnAck(_)).Times(1);
  ProcessAckPacket(&frame, true);
  EXPECT_TRUE(QuicConnectionPeer::GetReceivedTruncatedAck(&connection_));

  QuicConnectionPeer::SetMaxPacketsPerRetransmissionAlarm(&connection_, 200);
  const QuicTime::Delta kDefaultRetransmissionTime =
      QuicTime::Delta::FromMilliseconds(500);
  clock_.AdvanceTime(kDefaultRetransmissionTime);
  // Only packets that are less than largest observed should be retransmitted.
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(_, _)).Times(191);
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(191);
  connection_.OnRetransmissionTimeout();

  clock_.AdvanceTime(QuicTime::Delta::FromMicroseconds(
      2 * kDefaultRetransmissionTime.ToMicroseconds()));
  // Retransmit already retransmitted packets event though the sequence number
  // greater than the largest observed.
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(_, _)).Times(191);
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(191);
  connection_.OnRetransmissionTimeout();
}

TEST_F(QuicConnectionTest, LimitPacketsPerNack) {
  EXPECT_CALL(*send_algorithm_, OnIncomingAck(12, _, _)).Times(1);
  EXPECT_CALL(*send_algorithm_, OnIncomingLoss(_)).Times(1);
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(_, _)).Times(11);
  int offset = 0;
  // Send packets 1 to 12
  for (int i = 0; i < 12; ++i) {
    SendStreamDataToPeer(1, "foo", offset, !kFin, NULL);
    offset += 3;
  }

  // Ack 12, nack 1-11
  QuicAckFrame nack(12, QuicTime::Zero(), 0);
  for (int i = 1; i < 12; ++i) {
    nack.received_info.missing_packets.insert(i);
  }

  nack.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 12) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 11);
  SequenceNumberSet expected_acks;
  expected_acks.insert(12);
  EXPECT_CALL(visitor_, OnAck(ContainerEq(expected_acks)));

  // Nack three times.
  ProcessAckPacket(&nack, true);
  // The second call will trigger an ack.
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(1);
  ProcessAckPacket(&nack, true);
  // The third call should trigger retransmitting 10 packets.
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(10);
  ProcessAckPacket(&nack, true);

  // The fourth call should trigger retransmitting the 11th packet and an ack.
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(2);
  ProcessAckPacket(&nack, true);
}

// Test sending multiple acks from the connection to the session.
TEST_F(QuicConnectionTest, MultipleAcks) {
  EXPECT_CALL(*send_algorithm_, OnIncomingAck(_, _, _)).Times(6);
  EXPECT_CALL(*send_algorithm_, OnIncomingLoss(_)).Times(1);
  QuicPacketSequenceNumber last_packet;
  SendStreamDataToPeer(1u, "foo", 0, !kFin, &last_packet);  // Packet 1
  EXPECT_EQ(1u, last_packet);
  SendStreamDataToPeer(3u, "foo", 0, !kFin, &last_packet);  // Packet 2
  EXPECT_EQ(2u, last_packet);
  SendAckPacketToPeer();  // Packet 3
  SendStreamDataToPeer(5u, "foo", 0, !kFin, &last_packet);  // Packet 4
  EXPECT_EQ(4u, last_packet);
  SendStreamDataToPeer(1u, "foo", 3, !kFin, &last_packet);  // Packet 5
  EXPECT_EQ(5u, last_packet);
  SendStreamDataToPeer(3u, "foo", 3, !kFin, &last_packet);  // Packet 6
  EXPECT_EQ(6u, last_packet);

  // Client will ack packets 1, [!2], 3, 4, 5
  QuicAckFrame frame1(5, QuicTime::Zero(), 0);
  frame1.received_info.missing_packets.insert(2);
  frame1.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 5) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 2) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 1);

  // The connection should pass up acks for 1, 4, 5.  2 is not acked, and 3 was
  // an ackframe so should not be passed up.
  SequenceNumberSet expected_acks;
  expected_acks.insert(1);
  expected_acks.insert(4);
  expected_acks.insert(5);

  EXPECT_CALL(visitor_, OnAck(ContainerEq(expected_acks)));
  ProcessAckPacket(&frame1, true);

  // Now the client implicitly acks 2, and explicitly acks 6
  QuicAckFrame frame2(6, QuicTime::Zero(), 0);
  frame2.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 6);
  expected_acks.clear();
  // Both acks should be passed up.
  expected_acks.insert(2);
  expected_acks.insert(6);

  EXPECT_CALL(visitor_, OnAck(ContainerEq(expected_acks)));
  ProcessAckPacket(&frame2, true);
}

TEST_F(QuicConnectionTest, DontLatchUnackedPacket) {
  EXPECT_CALL(*send_algorithm_, OnIncomingAck(_, _, _)).Times(1);
  SendStreamDataToPeer(1, "foo", 0, !kFin, NULL);  // Packet 1;
  SendAckPacketToPeer();  // Packet 2

  // This sets least unacked to 3 (unsent packet), since we don't need
  // an ack for Packet 2 (ack packet).
  SequenceNumberSet expected_acks;
  expected_acks.insert(1);
  // Client acks packet 1
  EXPECT_CALL(visitor_, OnAck(ContainerEq(expected_acks)));
  QuicAckFrame frame(1, QuicTime::Zero(), 0);
  frame.received_info.entropy_hash = QuicConnectionPeer::GetSentEntropyHash(
      &connection_, 1);
  ProcessAckPacket(&frame, true);

  // Verify that our internal state has least-unacked as 3.
  EXPECT_EQ(3u, outgoing_ack()->sent_info.least_unacked);

  // When we send an ack, we make sure our least-unacked makes sense.  In this
  // case since we're not waiting on an ack for 2 and all packets are acked, we
  // set it to 3.
  SendAckPacketToPeer();  // Packet 3
  // Since this was an ack packet, we set least_unacked to 4.
  EXPECT_EQ(4u, outgoing_ack()->sent_info.least_unacked);
  // Check that the outgoing ack had its sequence number as least_unacked.
  EXPECT_EQ(3u, last_ack()->sent_info.least_unacked);

  SendStreamDataToPeer(1, "bar", 3, false, NULL);  // Packet 4
  EXPECT_EQ(4u, outgoing_ack()->sent_info.least_unacked);
  SendAckPacketToPeer();  // Packet 5
  EXPECT_EQ(4u, last_ack()->sent_info.least_unacked);
}

TEST_F(QuicConnectionTest, ReviveMissingPacketAfterFecPacket) {
  // Don't send missing packet 1.
  ProcessFecPacket(2, 1, true, !kEntropyFlag);
  EXPECT_FALSE(revived_header_.entropy_flag);
}

TEST_F(QuicConnectionTest, ReviveMissingPacketAfterDataPacketThenFecPacket) {
  ProcessFecProtectedPacket(1, false, kEntropyFlag);
  // Don't send missing packet 2.
  ProcessFecPacket(3, 1, true, !kEntropyFlag);
  EXPECT_TRUE(revived_header_.entropy_flag);
}

TEST_F(QuicConnectionTest, ReviveMissingPacketAfterDataPacketsThenFecPacket) {
  ProcessFecProtectedPacket(1, false, !kEntropyFlag);
  // Don't send missing packet 2.
  ProcessFecProtectedPacket(3, false, !kEntropyFlag);
  ProcessFecPacket(4, 1, true, kEntropyFlag);
  EXPECT_TRUE(revived_header_.entropy_flag);
}

TEST_F(QuicConnectionTest, ReviveMissingPacketAfterDataPacket) {
  // Don't send missing packet 1.
  ProcessFecPacket(3, 1, false, !kEntropyFlag);
  // out of order
  ProcessFecProtectedPacket(2, true, !kEntropyFlag);
  EXPECT_FALSE(revived_header_.entropy_flag);
}

TEST_F(QuicConnectionTest, ReviveMissingPacketAfterDataPackets) {
  ProcessFecProtectedPacket(1, false, !kEntropyFlag);
  // Don't send missing packet 2.
  ProcessFecPacket(6, 1, false, kEntropyFlag);
  ProcessFecProtectedPacket(3, false, kEntropyFlag);
  ProcessFecProtectedPacket(4, false, kEntropyFlag);
  ProcessFecProtectedPacket(5, true, !kEntropyFlag);
  EXPECT_TRUE(revived_header_.entropy_flag);
}

TEST_F(QuicConnectionTest, TestRetransmit) {
  const QuicTime::Delta kDefaultRetransmissionTime =
      QuicTime::Delta::FromMilliseconds(500);

  QuicTime default_retransmission_time = clock_.ApproximateNow().Add(
      kDefaultRetransmissionTime);
  SendStreamDataToPeer(1, "foo", 0, !kFin, NULL);
  EXPECT_EQ(1u, outgoing_ack()->sent_info.least_unacked);

  EXPECT_EQ(1u, last_header()->packet_sequence_number);
  EXPECT_EQ(default_retransmission_time, helper_->retransmission_alarm());
  // Simulate the retransimission alarm firing
  clock_.AdvanceTime(kDefaultRetransmissionTime);
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(1, _)).Times(1);
  connection_.RetransmitPacket(1);
  EXPECT_EQ(2u, last_header()->packet_sequence_number);
  EXPECT_EQ(2u, outgoing_ack()->sent_info.least_unacked);
}

TEST_F(QuicConnectionTest, RetransmitWithSameEncryptionLevel) {
  const QuicTime::Delta kDefaultRetransmissionTime =
      QuicTime::Delta::FromMilliseconds(500);

  QuicTime default_retransmission_time = clock_.ApproximateNow().Add(
      kDefaultRetransmissionTime);
  use_tagging_decrypter();

  // A TaggingEncrypter puts kTagSize copies of the given byte (0x01 here) at
  // the end of the packet. We can test this to check which encrypter was used.
  connection_.SetEncrypter(ENCRYPTION_NONE, new TaggingEncrypter(0x01));
  SendStreamDataToPeer(1, "foo", 0, !kFin, NULL);
  EXPECT_EQ(0x01010101u, final_bytes_of_last_packet());

  connection_.SetEncrypter(ENCRYPTION_INITIAL, new TaggingEncrypter(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  SendStreamDataToPeer(1, "foo", 0, !kFin, NULL);
  EXPECT_EQ(0x02020202u, final_bytes_of_last_packet());

  EXPECT_EQ(default_retransmission_time, helper_->retransmission_alarm());
  // Simulate the retransimission alarm firing
  clock_.AdvanceTime(kDefaultRetransmissionTime);
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(_, _)).Times(2);

  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  connection_.RetransmitPacket(1);
  // Packet should have been sent with ENCRYPTION_NONE.
  EXPECT_EQ(0x01010101u, final_bytes_of_last_packet());

  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  connection_.RetransmitPacket(2);
  // Packet should have been sent with ENCRYPTION_INITIAL.
  EXPECT_EQ(0x02020202u, final_bytes_of_last_packet());
}

TEST_F(QuicConnectionTest,
       DropRetransmitsForNullEncryptedPacketAfterForwardSecure) {
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_NONE, new TaggingEncrypter(0x01));
  QuicPacketSequenceNumber sequence_number;
  SendStreamDataToPeer(1, "foo", 0, !kFin, &sequence_number);

  connection_.SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                           new TaggingEncrypter(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);

  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(0);
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(sequence_number, _)).Times(1);

  const QuicTime::Delta kDefaultRetransmissionTime =
      QuicTime::Delta::FromMilliseconds(500);
  QuicTime default_retransmission_time = clock_.ApproximateNow().Add(
      kDefaultRetransmissionTime);

  EXPECT_EQ(default_retransmission_time, helper_->retransmission_alarm());
  // Simulate the retransimission alarm firing
  clock_.AdvanceTime(kDefaultRetransmissionTime);
  connection_.OnRetransmissionTimeout();
}

TEST_F(QuicConnectionTest, RetransmitPacketsWithInitialEncryption) {
  use_tagging_decrypter();
  connection_.SetEncrypter(ENCRYPTION_NONE, new TaggingEncrypter(0x01));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_NONE);

  SendStreamDataToPeer(1, "foo", 0, !kFin, NULL);

  connection_.SetEncrypter(ENCRYPTION_INITIAL, new TaggingEncrypter(0x02));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);

  SendStreamDataToPeer(2, "bar", 0, !kFin, NULL);

  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(1);
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(_, _)).Times(1);

  connection_.RetransmitUnackedPackets(QuicConnection::INITIAL_ENCRYPTION_ONLY);
}

TEST_F(QuicConnectionTest, BufferNonDecryptablePackets) {
  use_tagging_decrypter();

  const uint8 tag = 0x07;
  framer_.SetEncrypter(ENCRYPTION_INITIAL, new TaggingEncrypter(tag));

  // Process an encrypted packet which can not yet be decrypted
  // which should result in the packet being buffered.
  ProcessDataPacketAtLevel(1, false, kEntropyFlag, ENCRYPTION_INITIAL);

  // Transition to the new encryption state and process another
  // encrypted packet which should result in the original packet being
  // processed.
  connection_.SetDecrypter(new StrictTaggingDecrypter(tag));
  connection_.SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  connection_.SetEncrypter(ENCRYPTION_INITIAL, new TaggingEncrypter(tag));
  EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).Times(2).WillRepeatedly(
      Return(true));
  ProcessDataPacketAtLevel(2, false, kEntropyFlag, ENCRYPTION_INITIAL);

  // Finally, process a third packet and note that we do not
  // reprocess the buffered packet.
  EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).WillOnce(Return(true));
  ProcessDataPacketAtLevel(3, false, kEntropyFlag, ENCRYPTION_INITIAL);
}

TEST_F(QuicConnectionTest, TestRetransmitOrder) {
  QuicByteCount first_packet_size;
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).WillOnce(
      SaveArg<2>(&first_packet_size));
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(_, _)).Times(2);

  connection_.SendStreamData(1, "first_packet", 0, !kFin);
  QuicByteCount second_packet_size;
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).WillOnce(
      SaveArg<2>(&second_packet_size));
  connection_.SendStreamData(1, "second_packet", 12, !kFin);
  EXPECT_NE(first_packet_size, second_packet_size);
  // Advance the clock by huge time to make sure packets will be retransmitted.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(10));
  {
    InSequence s;
    EXPECT_CALL(*send_algorithm_,
                SentPacket(_, _, first_packet_size, _));
    EXPECT_CALL(*send_algorithm_,
                SentPacket(_, _, second_packet_size, _));
  }
  connection_.OnRetransmissionTimeout();
}

TEST_F(QuicConnectionTest, TestRetransmissionCountCalculation) {
  EXPECT_CALL(*send_algorithm_, OnIncomingLoss(_)).Times(1);
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(_, _)).Times(2);
  QuicPacketSequenceNumber original_sequence_number;
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, _, NOT_RETRANSMISSION))
      .WillOnce(SaveArg<1>(&original_sequence_number));
  connection_.SendStreamData(1, "foo", 0, !kFin);
  EXPECT_TRUE(QuicConnectionPeer::IsSavedForRetransmission(
      &connection_, original_sequence_number));
  EXPECT_EQ(0u, QuicConnectionPeer::GetRetransmissionCount(
      &connection_, original_sequence_number));
  // Force retransmission due to RTO.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(10));
  QuicPacketSequenceNumber rto_sequence_number;
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, _, IS_RETRANSMISSION))
      .WillOnce(SaveArg<1>(&rto_sequence_number));
  connection_.OnRetransmissionTimeout();
  EXPECT_FALSE(QuicConnectionPeer::IsSavedForRetransmission(
      &connection_, original_sequence_number));
  ASSERT_TRUE(QuicConnectionPeer::IsSavedForRetransmission(
      &connection_, rto_sequence_number));
  EXPECT_EQ(1u, QuicConnectionPeer::GetRetransmissionCount(
      &connection_, rto_sequence_number));
  // Once by explicit nack.
  QuicPacketSequenceNumber nack_sequence_number;
  // Ack packets might generate some other packets, which are not
  // retransmissions. (More ack packets).
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, NOT_RETRANSMISSION))
      .Times(AnyNumber());
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, IS_RETRANSMISSION))
      .WillOnce(SaveArg<1>(&nack_sequence_number));
  QuicAckFrame ack(rto_sequence_number, QuicTime::Zero(), 0);
  // Ack the retransmitted packet.
  ack.received_info.missing_packets.insert(rto_sequence_number);
  ack.received_info.entropy_hash = QuicConnectionPeer::GetSentEntropyHash(
      &connection_, rto_sequence_number - 1);
  for (int i = 0; i < 3; i++) {
    ProcessAckPacket(&ack, true);
  }
  EXPECT_FALSE(QuicConnectionPeer::IsSavedForRetransmission(
      &connection_, rto_sequence_number));
  EXPECT_TRUE(QuicConnectionPeer::IsSavedForRetransmission(
      &connection_, nack_sequence_number));
  EXPECT_EQ(2u, QuicConnectionPeer::GetRetransmissionCount(
      &connection_, nack_sequence_number));
}

TEST_F(QuicConnectionTest, SetRTOAfterWritingToSocket) {
  helper_->set_blocked(true);
  connection_.SendStreamData(1, "foo", 0, !kFin);
  // Make sure that RTO is not started when the packet is queued.
  EXPECT_EQ(0u, QuicConnectionPeer::GetNumRetransmissionTimeouts(&connection_));

  // Test that RTO is started once we write to the socket.
  helper_->set_blocked(false);
  EXPECT_CALL(visitor_, OnCanWrite());
  connection_.OnCanWrite();
  EXPECT_EQ(1u, QuicConnectionPeer::GetNumRetransmissionTimeouts(&connection_));
}

TEST_F(QuicConnectionTest, TestQueued) {
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  helper_->set_blocked(true);
  connection_.SendStreamData(1, "foo", 0, !kFin);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Attempt to send all packets, but since we're actually still
  // blocked, they should all remain queued.
  EXPECT_FALSE(connection_.OnCanWrite());
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Unblock the writes and actually send.
  helper_->set_blocked(false);
  EXPECT_CALL(visitor_, OnCanWrite());
  EXPECT_TRUE(connection_.OnCanWrite());
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, CloseFecGroup) {
  // Don't send missing packet 1
  // Don't send missing packet 2
  ProcessFecProtectedPacket(3, false, !kEntropyFlag);
  // Don't send missing FEC packet 3
  ASSERT_EQ(1u, connection_.NumFecGroups());

  // Now send non-fec protected ack packet and close the group
  QuicAckFrame frame(0, QuicTime::Zero(), 5);
  creator_.set_sequence_number(4);
  ProcessAckPacket(&frame, true);
  ASSERT_EQ(0u, connection_.NumFecGroups());
}

TEST_F(QuicConnectionTest, NoQuicCongestionFeedbackFrame) {
  SendAckPacketToPeer();
  EXPECT_TRUE(last_feedback() == NULL);
}

TEST_F(QuicConnectionTest, WithQuicCongestionFeedbackFrame) {
  QuicCongestionFeedbackFrame info;
  info.type = kFixRate;
  info.fix_rate.bitrate = QuicBandwidth::FromBytesPerSecond(123);
  SetFeedback(&info);

  SendAckPacketToPeer();
  EXPECT_EQ(kFixRate, last_feedback()->type);
  EXPECT_EQ(info.fix_rate.bitrate, last_feedback()->fix_rate.bitrate);
}

TEST_F(QuicConnectionTest, UpdateQuicCongestionFeedbackFrame) {
  SendAckPacketToPeer();
  EXPECT_CALL(*receive_algorithm_, RecordIncomingPacket(_, _, _, _));
  ProcessPacket(1);
}

TEST_F(QuicConnectionTest, DontUpdateQuicCongestionFeedbackFrameForRevived) {
  SendAckPacketToPeer();
  // Process an FEC packet, and revive the missing data packet
  // but only contact the receive_algorithm once.
  EXPECT_CALL(*receive_algorithm_, RecordIncomingPacket(_, _, _, _));
  ProcessFecPacket(2, 1, true, !kEntropyFlag);
}

TEST_F(QuicConnectionTest, InitialTimeout) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_CONNECTION_TIMED_OUT, false));
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));

  QuicTime default_timeout = clock_.ApproximateNow().Add(
      QuicTime::Delta::FromSeconds(kDefaultInitialTimeoutSecs));
  EXPECT_EQ(default_timeout, helper_->timeout_alarm());

  // Simulate the timeout alarm firing
  clock_.AdvanceTime(
      QuicTime::Delta::FromSeconds(kDefaultInitialTimeoutSecs));
  EXPECT_TRUE(connection_.CheckForTimeout());
  EXPECT_FALSE(connection_.connected());
}

TEST_F(QuicConnectionTest, TimeoutAfterSend) {
  EXPECT_TRUE(connection_.connected());

  QuicTime default_timeout = clock_.ApproximateNow().Add(
      QuicTime::Delta::FromSeconds(kDefaultInitialTimeoutSecs));

  // When we send a packet, the timeout will change to 5000 +
  // kDefaultInitialTimeoutSecs.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));

  // Send an ack so we don't set the retransimission alarm.
  SendAckPacketToPeer();
  EXPECT_EQ(default_timeout, helper_->timeout_alarm());

  // The original alarm will fire.  We should not time out because we had a
  // network event at t=5000.  The alarm will reregister.
  clock_.AdvanceTime(QuicTime::Delta::FromMicroseconds(
      kDefaultInitialTimeoutSecs * 1000000 - 5000));
  EXPECT_EQ(default_timeout, clock_.ApproximateNow());
  EXPECT_FALSE(connection_.CheckForTimeout());
  EXPECT_TRUE(connection_.connected());
  EXPECT_EQ(default_timeout.Add(QuicTime::Delta::FromMilliseconds(5)),
            helper_->timeout_alarm());

  // This time, we should time out.
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_CONNECTION_TIMED_OUT, false));
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(5));
  EXPECT_EQ(default_timeout.Add(QuicTime::Delta::FromMilliseconds(5)),
            clock_.ApproximateNow());
  EXPECT_TRUE(connection_.CheckForTimeout());
  EXPECT_FALSE(connection_.connected());
}

// TODO(ianswett): Add scheduler tests when should_retransmit is false.
TEST_F(QuicConnectionTest, SendScheduler) {
  // Test that if we send a packet without delay, it is not queued.
  QuicPacket* packet = ConstructDataPacket(1, 0, !kEntropyFlag);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::Zero()));
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  connection_.SendOrQueuePacket(
      ENCRYPTION_NONE, 1, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, SendSchedulerDelay) {
  // Test that if we send a packet with a delay, it ends up queued.
  QuicPacket* packet = ConstructDataPacket(1, 0, !kEntropyFlag);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::FromMicroseconds(1)));
  EXPECT_CALL(*send_algorithm_, SentPacket(_, 1, _, _)).Times(0);
  connection_.SendOrQueuePacket(
      ENCRYPTION_NONE, 1, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, SendSchedulerForce) {
  // Test that if we force send a packet, it is not queued.
  QuicPacket* packet = ConstructDataPacket(1, 0, !kEntropyFlag);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, IS_RETRANSMISSION, _, _)).Times(0);
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  connection_.SendOrQueuePacket(
      ENCRYPTION_NONE, 1, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
  // XXX: fixme.  was:  connection_.SendOrQueuePacket(1, packet, kForce);
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, SendSchedulerEAGAIN) {
  QuicPacket* packet = ConstructDataPacket(1, 0, !kEntropyFlag);
  helper_->set_blocked(true);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::Zero()));
  EXPECT_CALL(*send_algorithm_, SentPacket(_, 1, _, _)).Times(0);
  connection_.SendOrQueuePacket(
      ENCRYPTION_NONE, 1, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, SendSchedulerDelayThenSend) {
  // Test that if we send a packet with a delay, it ends up queued.
  QuicPacket* packet = ConstructDataPacket(1, 0, !kEntropyFlag);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::FromMicroseconds(1)));
  connection_.SendOrQueuePacket(
       ENCRYPTION_NONE, 1, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Advance the clock to fire the alarm, and configure the scheduler
  // to permit the packet to be sent.
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillRepeatedly(
                  testing::Return(QuicTime::Delta::Zero()));
  clock_.AdvanceTime(QuicTime::Delta::FromMicroseconds(1));
  helper_->UnregisterSendAlarmIfRegistered();
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _));
  EXPECT_CALL(visitor_, OnCanWrite());
  connection_.OnCanWrite();
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, SendSchedulerDelayThenRetransmit) {
  EXPECT_CALL(*send_algorithm_, TimeUntilSend(_, NOT_RETRANSMISSION, _, _))
      .WillRepeatedly(testing::Return(QuicTime::Delta::Zero()));
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(1, _)).Times(1);
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, 1, _, NOT_RETRANSMISSION));
  connection_.SendStreamData(1, "foo", 0, !kFin);
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  // Advance the time for retransmission of lost packet.
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(501));
  // Test that if we send a retransmit with a delay, it ends up queued.
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, IS_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::FromMicroseconds(1)));
  connection_.OnRetransmissionTimeout();
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Advance the clock to fire the alarm, and configure the scheduler
  // to permit the packet to be sent.
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, IS_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::Zero()));

  // Ensure the scheduler is notified this is a retransmit.
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, _, IS_RETRANSMISSION));
  clock_.AdvanceTime(QuicTime::Delta::FromMicroseconds(1));
  helper_->UnregisterSendAlarmIfRegistered();
  EXPECT_CALL(visitor_, OnCanWrite());
  connection_.OnCanWrite();
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, SendSchedulerDelayAndQueue) {
  QuicPacket* packet = ConstructDataPacket(1, 0, !kEntropyFlag);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::FromMicroseconds(1)));
  connection_.SendOrQueuePacket(
      ENCRYPTION_NONE, 1, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Attempt to send another packet and make sure that it gets queued.
  packet = ConstructDataPacket(2, 0, !kEntropyFlag);
  connection_.SendOrQueuePacket(
      ENCRYPTION_NONE, 2, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
  EXPECT_EQ(2u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, SendSchedulerDelayThenAckAndSend) {
  QuicPacket* packet = ConstructDataPacket(1, 0, !kEntropyFlag);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::FromMicroseconds(10)));
  connection_.SendOrQueuePacket(
      ENCRYPTION_NONE, 1, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Now send non-retransmitting information, that we're not going to
  // retransmit 3. The far end should stop waiting for it.
  QuicAckFrame frame(0, QuicTime::Zero(), 1);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillRepeatedly(
                  testing::Return(QuicTime::Delta::Zero()));
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, _, _));
  ProcessAckPacket(&frame, true);

  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  // Ensure alarm is not set
  EXPECT_FALSE(helper_->IsSendAlarmSet());
}

TEST_F(QuicConnectionTest, SendSchedulerDelayThenAckAndHold) {
  QuicPacket* packet = ConstructDataPacket(1, 0, !kEntropyFlag);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::FromMicroseconds(10)));
  connection_.SendOrQueuePacket(
      ENCRYPTION_NONE, 1, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // Now send non-retransmitting information, that we're not going to
  // retransmit 3.  The far end should stop waiting for it.
  QuicAckFrame frame(0, QuicTime::Zero(), 1);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::FromMicroseconds(1)));
  ProcessAckPacket(&frame, false);

  EXPECT_EQ(1u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, SendSchedulerDelayThenOnCanWrite) {
  QuicPacket* packet = ConstructDataPacket(1, 0, !kEntropyFlag);
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::FromMicroseconds(10)));
  connection_.SendOrQueuePacket(
      ENCRYPTION_NONE, 1, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());

  // OnCanWrite should not send the packet (because of the delay)
  // but should still return true.
  EXPECT_TRUE(connection_.OnCanWrite());
  EXPECT_EQ(1u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, TestQueueLimitsOnSendStreamData) {
  // All packets carry version info till version is negotiated.
  size_t payload_length;
  connection_.options()->max_packet_length =
      GetPacketLengthForOneStream(
          kIncludeVersion, NOT_IN_FEC_GROUP, &payload_length);

  // Queue the first packet.
  EXPECT_CALL(*send_algorithm_,
              TimeUntilSend(_, NOT_RETRANSMISSION, _, _)).WillOnce(
                  testing::Return(QuicTime::Delta::FromMicroseconds(10)));
  const string payload(payload_length, 'a');
  EXPECT_EQ(0u,
            connection_.SendStreamData(1, payload, 0, !kFin).bytes_consumed);
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
}

TEST_F(QuicConnectionTest, LoopThroughSendingPackets) {
  // All packets carry version info till version is negotiated.
  size_t payload_length;
  connection_.options()->max_packet_length =
      GetPacketLengthForOneStream(
          kIncludeVersion, NOT_IN_FEC_GROUP, &payload_length);

  // Queue the first packet.
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(7);
  // TODO(ianswett): The first stream frame will consume 2 fewer bytes.
  const string payload(payload_length * 7, 'a');
  EXPECT_EQ(payload.size(),
            connection_.SendStreamData(1, payload, 0, !kFin).bytes_consumed);
}

TEST_F(QuicConnectionTest, NoAckForClose) {
  ProcessPacket(1);
  EXPECT_CALL(*send_algorithm_, OnIncomingAck(_, _, _)).Times(0);
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_PEER_GOING_AWAY, true));
  EXPECT_CALL(*send_algorithm_, SentPacket(_, _, _, _)).Times(0);
  ProcessClosePacket(2, 0);
}

TEST_F(QuicConnectionTest, SendWhenDisconnected) {
  EXPECT_TRUE(connection_.connected());
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_PEER_GOING_AWAY, false));
  connection_.CloseConnection(QUIC_PEER_GOING_AWAY, false);
  EXPECT_FALSE(connection_.connected());
  QuicPacket* packet = ConstructDataPacket(1, 0, !kEntropyFlag);
  EXPECT_CALL(*send_algorithm_, SentPacket(_, 1, _, _)).Times(0);
  connection_.SendOrQueuePacket(
      ENCRYPTION_NONE, 1, packet, kTestEntropyHash, HAS_RETRANSMITTABLE_DATA);
}

TEST_F(QuicConnectionTest, PublicReset) {
  QuicPublicResetPacket header;
  header.public_header.guid = guid_;
  header.public_header.reset_flag = true;
  header.public_header.version_flag = false;
  header.rejected_sequence_number = 10101;
  scoped_ptr<QuicEncryptedPacket> packet(
      framer_.ConstructPublicResetPacket(header));
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_PUBLIC_RESET, true));
  connection_.ProcessUdpPacket(IPEndPoint(), IPEndPoint(), *packet);
}

TEST_F(QuicConnectionTest, GoAway) {
  QuicGoAwayFrame goaway;
  goaway.last_good_stream_id = 1;
  goaway.error_code = QUIC_PEER_GOING_AWAY;
  goaway.reason_phrase = "Going away.";
  EXPECT_CALL(visitor_, OnGoAway(_));
  ProcessGoAwayPacket(&goaway);
}

TEST_F(QuicConnectionTest, MissingPacketsBeforeLeastUnacked) {
  QuicAckFrame ack(0, QuicTime::Zero(), 4);
  // Set the sequence number of the ack packet to be least unacked (4)
  creator_.set_sequence_number(3);
  ProcessAckPacket(&ack, true);
  EXPECT_TRUE(outgoing_ack()->received_info.missing_packets.empty());
}

TEST_F(QuicConnectionTest, ReceivedEntropyHashCalculation) {
  EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).WillRepeatedly(Return(true));
  ProcessDataPacket(1, 1, kEntropyFlag);
  ProcessDataPacket(4, 1, kEntropyFlag);
  ProcessDataPacket(3, 1, !kEntropyFlag);
  ProcessDataPacket(7, 1, kEntropyFlag);
  EXPECT_EQ(146u, outgoing_ack()->received_info.entropy_hash);
}

TEST_F(QuicConnectionTest, UpdateEntropyForReceivedPackets) {
  EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).WillRepeatedly(Return(true));
  ProcessDataPacket(1, 1, kEntropyFlag);
  ProcessDataPacket(5, 1, kEntropyFlag);
  ProcessDataPacket(4, 1, !kEntropyFlag);
  EXPECT_EQ(34u, outgoing_ack()->received_info.entropy_hash);
  // Make 4th packet my least unacked, and update entropy for 2, 3 packets.
  QuicAckFrame ack(0, QuicTime::Zero(), 4);
  QuicPacketEntropyHash kRandomEntropyHash = 129u;
  ack.sent_info.entropy_hash = kRandomEntropyHash;
  creator_.set_sequence_number(5);
  QuicPacketEntropyHash six_packet_entropy_hash = 0;
  if (ProcessAckPacket(&ack, true)) {
    six_packet_entropy_hash = 1 << 6;
  };

  EXPECT_EQ((kRandomEntropyHash + (1 << 5) + six_packet_entropy_hash),
            outgoing_ack()->received_info.entropy_hash);
}

TEST_F(QuicConnectionTest, UpdateEntropyHashUptoCurrentPacket) {
  EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).WillRepeatedly(Return(true));
  ProcessDataPacket(1, 1, kEntropyFlag);
  ProcessDataPacket(5, 1, !kEntropyFlag);
  ProcessDataPacket(22, 1, kEntropyFlag);
  EXPECT_EQ(66u, outgoing_ack()->received_info.entropy_hash);
  creator_.set_sequence_number(22);
  QuicPacketEntropyHash kRandomEntropyHash = 85u;
  // Current packet is the least unacked packet.
  QuicAckFrame ack(0, QuicTime::Zero(), 23);
  ack.sent_info.entropy_hash = kRandomEntropyHash;
  QuicPacketEntropyHash ack_entropy_hash =  ProcessAckPacket(&ack, true);
  EXPECT_EQ((kRandomEntropyHash + ack_entropy_hash),
            outgoing_ack()->received_info.entropy_hash);
  ProcessDataPacket(25, 1, kEntropyFlag);
  EXPECT_EQ((kRandomEntropyHash + ack_entropy_hash + (1 << (25 % 8))),
            outgoing_ack()->received_info.entropy_hash);
}

TEST_F(QuicConnectionTest, EntropyCalculationForTruncatedAck) {
  EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).WillRepeatedly(Return(true));
  QuicPacketEntropyHash entropy[51];
  entropy[0] = 0;
  for (int i = 1; i < 51; ++i) {
    bool should_send = i % 10 != 0;
    bool entropy_flag = (i & (i - 1)) != 0;
    if (!should_send) {
      entropy[i] = entropy[i - 1];
      continue;
    }
    if (entropy_flag) {
      entropy[i] = entropy[i - 1] ^ (1 << (i % 8));
    } else {
      entropy[i] = entropy[i - 1];
    }
    ProcessDataPacket(i, 1, entropy_flag);
  }
  // Till 50 since 50th packet is not sent.
  for (int i = 1; i < 50; ++i) {
    EXPECT_EQ(entropy[i], QuicConnectionPeer::ReceivedEntropyHash(
        &connection_, i));
  }
}

TEST_F(QuicConnectionTest, CheckSentEntropyHash) {
  creator_.set_sequence_number(1);
  SequenceNumberSet missing_packets;
  QuicPacketEntropyHash entropy_hash = 0;
  QuicPacketSequenceNumber max_sequence_number = 51;
  for (QuicPacketSequenceNumber i = 1; i <= max_sequence_number; ++i) {
    bool is_missing = i % 10 != 0;
    bool entropy_flag = (i & (i - 1)) != 0;
    QuicPacketEntropyHash packet_entropy_hash = 0;
    if (entropy_flag) {
      packet_entropy_hash = 1 << (i % 8);
    }
    QuicPacket* packet = ConstructDataPacket(i, 0, entropy_flag);
    connection_.SendOrQueuePacket(
        ENCRYPTION_NONE, i, packet, packet_entropy_hash,
        HAS_RETRANSMITTABLE_DATA);

    if (is_missing)  {
      missing_packets.insert(i);
      continue;
    }

    entropy_hash ^= packet_entropy_hash;
  }
  EXPECT_TRUE(QuicConnectionPeer::IsValidEntropy(
      &connection_, max_sequence_number, missing_packets, entropy_hash))
      << "";
}

// TODO(satyamsehkhar): Add more test when we start supporting more versions.
TEST_F(QuicConnectionTest, SendVersionNegotiationPacket) {
  // TODO(rjshade): Update this to use a real version once we have multiple
  //                versions in the codebase.
  framer_.set_version_for_tests(QUIC_VERSION_UNSUPPORTED);

  QuicPacketHeader header;
  header.public_header.guid = guid_;
  header.public_header.reset_flag = false;
  header.public_header.version_flag = true;
  header.entropy_flag = false;
  header.fec_flag = false;
  header.packet_sequence_number = 12;
  header.fec_group = 0;

  QuicFrames frames;
  QuicFrame frame(&frame1_);
  frames.push_back(frame);
  scoped_ptr<QuicPacket> packet(
      framer_.ConstructFrameDataPacket(header, frames).packet);
  scoped_ptr<QuicEncryptedPacket> encrypted(
      framer_.EncryptPacket(ENCRYPTION_NONE, 12, *packet));

  framer_.set_version(QuicVersionMax());
  connection_.set_is_server(true);
  connection_.ProcessUdpPacket(IPEndPoint(), IPEndPoint(), *encrypted);
  EXPECT_TRUE(helper_->version_negotiation_packet() != NULL);

  size_t num_versions = arraysize(kSupportedQuicVersions);
  EXPECT_EQ(num_versions,
            helper_->version_negotiation_packet()->versions.size());

  // We expect all versions in kSupportedQuicVersions to be
  // included in the packet.
  for (size_t i = 0; i < num_versions; ++i) {
    EXPECT_EQ(kSupportedQuicVersions[i],
              helper_->version_negotiation_packet()->versions[i]);
  }
}

TEST_F(QuicConnectionTest, CheckSendStats) {
  EXPECT_CALL(*send_algorithm_, AbandoningPacket(_, _)).Times(3);
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, _, NOT_RETRANSMISSION));
  connection_.SendStreamData(1u, "first", 0, !kFin);
  size_t first_packet_size = last_sent_packet_size();

  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, _, NOT_RETRANSMISSION)).Times(2);
  connection_.SendStreamData(1u, "second", 0, !kFin);
  size_t second_packet_size = last_sent_packet_size();

  // 2 retransmissions due to rto, 1 due to explicit nack.
  EXPECT_CALL(*send_algorithm_,
              SentPacket(_, _, _, IS_RETRANSMISSION)).Times(3);

  // Retransmit due to RTO.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(10));
  connection_.OnRetransmissionTimeout();

  // Retransmit due to explicit nacks
  QuicAckFrame nack_three(4, QuicTime::Zero(), 0);
  nack_three.received_info.missing_packets.insert(3);
  nack_three.received_info.entropy_hash =
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 4) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 3) ^
      QuicConnectionPeer::GetSentEntropyHash(&connection_, 2);
  QuicFrame frame(&nack_three);
  EXPECT_CALL(visitor_, OnAck(_));
  EXPECT_CALL(*send_algorithm_, OnIncomingAck(_, _, _)).Times(1);
  EXPECT_CALL(*send_algorithm_, OnIncomingLoss(_)).Times(1);
  EXPECT_CALL(visitor_, OnCanWrite()).Times(3).WillRepeatedly(Return(true));

  ProcessFramePacket(frame);
  ProcessFramePacket(frame);
  size_t ack_packet_size = last_sent_packet_size();
  ProcessFramePacket(frame);

  EXPECT_CALL(*send_algorithm_, SmoothedRtt()).WillOnce(
      Return(QuicTime::Delta::Zero()));
  EXPECT_CALL(*send_algorithm_, BandwidthEstimate()).WillOnce(
      Return(QuicBandwidth::Zero()));

  const QuicConnectionStats& stats = connection_.GetStats();
  EXPECT_EQ(3 * first_packet_size + 2 * second_packet_size + ack_packet_size -
            kQuicVersionSize, stats.bytes_sent);
  EXPECT_EQ(6u, stats.packets_sent);
  EXPECT_EQ(2 * first_packet_size + second_packet_size - kQuicVersionSize,
            stats.bytes_retransmitted);
  EXPECT_EQ(3u, stats.packets_retransmitted);
  EXPECT_EQ(2u, stats.rto_count);
}

TEST_F(QuicConnectionTest, CheckReceiveStats) {
  size_t received_bytes = 0;
  received_bytes += ProcessFecProtectedPacket(1, false, !kEntropyFlag);
  received_bytes += ProcessFecProtectedPacket(3, false, !kEntropyFlag);
  // Should be counted against dropped packets.
  received_bytes += ProcessDataPacket(3, 1, !kEntropyFlag);
  received_bytes += ProcessFecPacket(4, 1, true, !kEntropyFlag);  // Fec packet

  EXPECT_CALL(*send_algorithm_, SmoothedRtt()).WillOnce(
      Return(QuicTime::Delta::Zero()));
  EXPECT_CALL(*send_algorithm_, BandwidthEstimate()).WillOnce(
      Return(QuicBandwidth::Zero()));

  const QuicConnectionStats& stats = connection_.GetStats();
  EXPECT_EQ(received_bytes, stats.bytes_received);
  EXPECT_EQ(4u, stats.packets_received);

  EXPECT_EQ(1u, stats.packets_revived);
  EXPECT_EQ(1u, stats.packets_dropped);
}

TEST_F(QuicConnectionTest, TestFecGroupLimits) {
  // Create and return a group for 1
  ASSERT_TRUE(QuicConnectionPeer::GetFecGroup(&connection_, 1) != NULL);

  // Create and return a group for 2
  ASSERT_TRUE(QuicConnectionPeer::GetFecGroup(&connection_, 2) != NULL);

  // Create and return a group for 4.  This should remove 1 but not 2.
  ASSERT_TRUE(QuicConnectionPeer::GetFecGroup(&connection_, 4) != NULL);
  ASSERT_TRUE(QuicConnectionPeer::GetFecGroup(&connection_, 1) == NULL);
  ASSERT_TRUE(QuicConnectionPeer::GetFecGroup(&connection_, 2) != NULL);

  // Create and return a group for 3.  This will kill off 2.
  ASSERT_TRUE(QuicConnectionPeer::GetFecGroup(&connection_, 3) != NULL);
  ASSERT_TRUE(QuicConnectionPeer::GetFecGroup(&connection_, 2) == NULL);

  // Verify that adding 5 kills off 3, despite 4 being created before 3.
  ASSERT_TRUE(QuicConnectionPeer::GetFecGroup(&connection_, 5) != NULL);
  ASSERT_TRUE(QuicConnectionPeer::GetFecGroup(&connection_, 4) != NULL);
  ASSERT_TRUE(QuicConnectionPeer::GetFecGroup(&connection_, 3) == NULL);
}

TEST_F(QuicConnectionTest, DontProcessFramesIfPacketClosedConnection) {
  // Construct a packet with stream frame and connection close frame.
  header_.public_header.guid = guid_;
  header_.packet_sequence_number = 1;
  header_.public_header.reset_flag = false;
  header_.public_header.version_flag = false;
  header_.entropy_flag = false;
  header_.fec_flag = false;
  header_.fec_group = 0;

  QuicConnectionCloseFrame qccf;
  qccf.error_code = QUIC_PEER_GOING_AWAY;
  qccf.ack_frame = QuicAckFrame(0, QuicTime::Zero(), 1);
  QuicFrame close_frame(&qccf);
  QuicFrame stream_frame(&frame1_);

  QuicFrames frames;
  frames.push_back(stream_frame);
  frames.push_back(close_frame);
  scoped_ptr<QuicPacket> packet(
      framer_.ConstructFrameDataPacket(header_, frames).packet);
  EXPECT_TRUE(NULL != packet.get());
  scoped_ptr<QuicEncryptedPacket> encrypted(framer_.EncryptPacket(
      ENCRYPTION_NONE, 1, *packet));

  EXPECT_CALL(visitor_, OnCanWrite()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_PEER_GOING_AWAY, true));
  EXPECT_CALL(visitor_, OnPacket(_, _, _, _)).Times(0);

  connection_.ProcessUdpPacket(IPEndPoint(), IPEndPoint(), *encrypted);
}

//// The QUIC_VERSION_X versions are deliberately set, rather than using all
//// values in kSupportedQuicVersions.
//TEST_F(QuicConnectionTest, SelectMutualVersion) {
//  // Set the connection to speak QUIC_VERSION_6.
//  connection_.set_version(QUIC_VERSION_6);
//  EXPECT_EQ(connection_.version(), QUIC_VERSION_6);
//
//  // Pass in available versions which includes a higher mutually supported
//  // version.  The higher mutually supported version should be selected.
//  EXPECT_TRUE(
//      connection_.SelectMutualVersion({QUIC_VERSION_6, QUIC_VERSION_7}));
//  EXPECT_EQ(connection_.version(), QUIC_VERSION_7);
//
//  // Expect that the lower version is selected.
//  EXPECT_TRUE(connection_.SelectMutualVersion({QUIC_VERSION_6}));
//  EXPECT_EQ(connection_.version(), QUIC_VERSION_6);
//
//  // Shouldn't be able to find a mutually supported version.
//  EXPECT_FALSE(connection_.SelectMutualVersion({QUIC_VERSION_UNSUPPORTED}));
//}

TEST_F(QuicConnectionTest, ConnectionCloseWhenNotWriteBlocked) {
  helper_->set_blocked(false);  // Already default.

  // Send a packet (but write will not block).
  ProcessFecPacket(2, 1, true, !kEntropyFlag);
  EXPECT_EQ(0u, connection_.NumQueuedPackets());
  EXPECT_EQ(1u, helper_->packets_write_attempts());

  // Send an erroneous packet to close the connection.
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_INVALID_PACKET_HEADER, false));
  ProcessDataPacket(6000, 0, !kEntropyFlag);
  EXPECT_EQ(2u, helper_->packets_write_attempts());
}

TEST_F(QuicConnectionTest, ConnectionCloseWhenWriteBlocked) {
  helper_->set_blocked(true);

  // Send a packet to so that write will really block.
  ProcessFecPacket(2, 1, true, !kEntropyFlag);
  EXPECT_EQ(1u, connection_.NumQueuedPackets());
  EXPECT_EQ(1u, helper_->packets_write_attempts());

  // Send an erroneous packet to close the connection.
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_INVALID_PACKET_HEADER, false));
  ProcessDataPacket(6000, 0, !kEntropyFlag);
  EXPECT_EQ(1u, helper_->packets_write_attempts());
}

TEST_F(QuicConnectionTest, ConnectionCloseWhenNothingPending) {
  helper_->set_blocked(true);

  // Send an erroneous packet to close the connection.
  EXPECT_CALL(visitor_, ConnectionClose(QUIC_INVALID_PACKET_HEADER, false));
  ProcessDataPacket(6000, 0, !kEntropyFlag);
  EXPECT_EQ(1u, helper_->packets_write_attempts());
}

}  // namespace
}  // namespace test
}  // namespace net
