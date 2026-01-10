package com.meshcore.team.data.database;

import android.database.Cursor;
import android.os.CancellationSignal;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.room.CoroutinesRoom;
import androidx.room.EntityDeletionOrUpdateAdapter;
import androidx.room.EntityInsertionAdapter;
import androidx.room.RoomDatabase;
import androidx.room.RoomSQLiteQuery;
import androidx.room.SharedSQLiteStatement;
import androidx.room.util.CursorUtil;
import androidx.room.util.DBUtil;
import androidx.sqlite.db.SupportSQLiteStatement;
import java.lang.Class;
import java.lang.Exception;
import java.lang.Object;
import java.lang.Override;
import java.lang.String;
import java.lang.SuppressWarnings;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.Callable;
import javax.annotation.processing.Generated;
import kotlin.Unit;
import kotlin.coroutines.Continuation;
import kotlinx.coroutines.flow.Flow;

@Generated("androidx.room.RoomProcessor")
@SuppressWarnings({"unchecked", "deprecation"})
public final class MessageDao_Impl implements MessageDao {
  private final RoomDatabase __db;

  private final EntityInsertionAdapter<MessageEntity> __insertionAdapterOfMessageEntity;

  private final EntityDeletionOrUpdateAdapter<MessageEntity> __deletionAdapterOfMessageEntity;

  private final EntityDeletionOrUpdateAdapter<MessageEntity> __updateAdapterOfMessageEntity;

  private final SharedSQLiteStatement __preparedStmtOfDeleteMessagesByChannel;

  private final SharedSQLiteStatement __preparedStmtOfUpdateDeliveryStatus;

  public MessageDao_Impl(@NonNull final RoomDatabase __db) {
    this.__db = __db;
    this.__insertionAdapterOfMessageEntity = new EntityInsertionAdapter<MessageEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "INSERT OR REPLACE INTO `messages` (`id`,`senderId`,`senderName`,`channelHash`,`content`,`timestamp`,`isPrivate`,`ackChecksum`,`deliveryStatus`,`heardByCount`,`attempt`,`isSentByMe`) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final MessageEntity entity) {
        statement.bindString(1, entity.getId());
        statement.bindBlob(2, entity.getSenderId());
        if (entity.getSenderName() == null) {
          statement.bindNull(3);
        } else {
          statement.bindString(3, entity.getSenderName());
        }
        statement.bindLong(4, entity.getChannelHash());
        statement.bindString(5, entity.getContent());
        statement.bindLong(6, entity.getTimestamp());
        final int _tmp = entity.isPrivate() ? 1 : 0;
        statement.bindLong(7, _tmp);
        if (entity.getAckChecksum() == null) {
          statement.bindNull(8);
        } else {
          statement.bindBlob(8, entity.getAckChecksum());
        }
        statement.bindString(9, entity.getDeliveryStatus());
        statement.bindLong(10, entity.getHeardByCount());
        statement.bindLong(11, entity.getAttempt());
        final int _tmp_1 = entity.isSentByMe() ? 1 : 0;
        statement.bindLong(12, _tmp_1);
      }
    };
    this.__deletionAdapterOfMessageEntity = new EntityDeletionOrUpdateAdapter<MessageEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "DELETE FROM `messages` WHERE `id` = ?";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final MessageEntity entity) {
        statement.bindString(1, entity.getId());
      }
    };
    this.__updateAdapterOfMessageEntity = new EntityDeletionOrUpdateAdapter<MessageEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "UPDATE OR ABORT `messages` SET `id` = ?,`senderId` = ?,`senderName` = ?,`channelHash` = ?,`content` = ?,`timestamp` = ?,`isPrivate` = ?,`ackChecksum` = ?,`deliveryStatus` = ?,`heardByCount` = ?,`attempt` = ?,`isSentByMe` = ? WHERE `id` = ?";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final MessageEntity entity) {
        statement.bindString(1, entity.getId());
        statement.bindBlob(2, entity.getSenderId());
        if (entity.getSenderName() == null) {
          statement.bindNull(3);
        } else {
          statement.bindString(3, entity.getSenderName());
        }
        statement.bindLong(4, entity.getChannelHash());
        statement.bindString(5, entity.getContent());
        statement.bindLong(6, entity.getTimestamp());
        final int _tmp = entity.isPrivate() ? 1 : 0;
        statement.bindLong(7, _tmp);
        if (entity.getAckChecksum() == null) {
          statement.bindNull(8);
        } else {
          statement.bindBlob(8, entity.getAckChecksum());
        }
        statement.bindString(9, entity.getDeliveryStatus());
        statement.bindLong(10, entity.getHeardByCount());
        statement.bindLong(11, entity.getAttempt());
        final int _tmp_1 = entity.isSentByMe() ? 1 : 0;
        statement.bindLong(12, _tmp_1);
        statement.bindString(13, entity.getId());
      }
    };
    this.__preparedStmtOfDeleteMessagesByChannel = new SharedSQLiteStatement(__db) {
      @Override
      @NonNull
      public String createQuery() {
        final String _query = "DELETE FROM messages WHERE channelHash = ?";
        return _query;
      }
    };
    this.__preparedStmtOfUpdateDeliveryStatus = new SharedSQLiteStatement(__db) {
      @Override
      @NonNull
      public String createQuery() {
        final String _query = "UPDATE messages SET deliveryStatus = ?, heardByCount = ? WHERE id = ?";
        return _query;
      }
    };
  }

  @Override
  public Object insertMessage(final MessageEntity message,
      final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __insertionAdapterOfMessageEntity.insert(message);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object deleteMessage(final MessageEntity message,
      final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __deletionAdapterOfMessageEntity.handle(message);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object updateMessage(final MessageEntity message,
      final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __updateAdapterOfMessageEntity.handle(message);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object deleteMessagesByChannel(final byte channelHash,
      final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        final SupportSQLiteStatement _stmt = __preparedStmtOfDeleteMessagesByChannel.acquire();
        int _argIndex = 1;
        _stmt.bindLong(_argIndex, channelHash);
        try {
          __db.beginTransaction();
          try {
            _stmt.executeUpdateDelete();
            __db.setTransactionSuccessful();
            return Unit.INSTANCE;
          } finally {
            __db.endTransaction();
          }
        } finally {
          __preparedStmtOfDeleteMessagesByChannel.release(_stmt);
        }
      }
    }, $completion);
  }

  @Override
  public Object updateDeliveryStatus(final String messageId, final String status, final int count,
      final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        final SupportSQLiteStatement _stmt = __preparedStmtOfUpdateDeliveryStatus.acquire();
        int _argIndex = 1;
        _stmt.bindString(_argIndex, status);
        _argIndex = 2;
        _stmt.bindLong(_argIndex, count);
        _argIndex = 3;
        _stmt.bindString(_argIndex, messageId);
        try {
          __db.beginTransaction();
          try {
            _stmt.executeUpdateDelete();
            __db.setTransactionSuccessful();
            return Unit.INSTANCE;
          } finally {
            __db.endTransaction();
          }
        } finally {
          __preparedStmtOfUpdateDeliveryStatus.release(_stmt);
        }
      }
    }, $completion);
  }

  @Override
  public Flow<List<MessageEntity>> getMessagesByChannel(final byte channelHash) {
    final String _sql = "SELECT * FROM messages WHERE channelHash = ? ORDER BY timestamp ASC";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 1);
    int _argIndex = 1;
    _statement.bindLong(_argIndex, channelHash);
    return CoroutinesRoom.createFlow(__db, false, new String[] {"messages"}, new Callable<List<MessageEntity>>() {
      @Override
      @NonNull
      public List<MessageEntity> call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfId = CursorUtil.getColumnIndexOrThrow(_cursor, "id");
          final int _cursorIndexOfSenderId = CursorUtil.getColumnIndexOrThrow(_cursor, "senderId");
          final int _cursorIndexOfSenderName = CursorUtil.getColumnIndexOrThrow(_cursor, "senderName");
          final int _cursorIndexOfChannelHash = CursorUtil.getColumnIndexOrThrow(_cursor, "channelHash");
          final int _cursorIndexOfContent = CursorUtil.getColumnIndexOrThrow(_cursor, "content");
          final int _cursorIndexOfTimestamp = CursorUtil.getColumnIndexOrThrow(_cursor, "timestamp");
          final int _cursorIndexOfIsPrivate = CursorUtil.getColumnIndexOrThrow(_cursor, "isPrivate");
          final int _cursorIndexOfAckChecksum = CursorUtil.getColumnIndexOrThrow(_cursor, "ackChecksum");
          final int _cursorIndexOfDeliveryStatus = CursorUtil.getColumnIndexOrThrow(_cursor, "deliveryStatus");
          final int _cursorIndexOfHeardByCount = CursorUtil.getColumnIndexOrThrow(_cursor, "heardByCount");
          final int _cursorIndexOfAttempt = CursorUtil.getColumnIndexOrThrow(_cursor, "attempt");
          final int _cursorIndexOfIsSentByMe = CursorUtil.getColumnIndexOrThrow(_cursor, "isSentByMe");
          final List<MessageEntity> _result = new ArrayList<MessageEntity>(_cursor.getCount());
          while (_cursor.moveToNext()) {
            final MessageEntity _item;
            final String _tmpId;
            _tmpId = _cursor.getString(_cursorIndexOfId);
            final byte[] _tmpSenderId;
            _tmpSenderId = _cursor.getBlob(_cursorIndexOfSenderId);
            final String _tmpSenderName;
            if (_cursor.isNull(_cursorIndexOfSenderName)) {
              _tmpSenderName = null;
            } else {
              _tmpSenderName = _cursor.getString(_cursorIndexOfSenderName);
            }
            final byte _tmpChannelHash;
            _tmpChannelHash = (byte) (_cursor.getShort(_cursorIndexOfChannelHash));
            final String _tmpContent;
            _tmpContent = _cursor.getString(_cursorIndexOfContent);
            final long _tmpTimestamp;
            _tmpTimestamp = _cursor.getLong(_cursorIndexOfTimestamp);
            final boolean _tmpIsPrivate;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsPrivate);
            _tmpIsPrivate = _tmp != 0;
            final byte[] _tmpAckChecksum;
            if (_cursor.isNull(_cursorIndexOfAckChecksum)) {
              _tmpAckChecksum = null;
            } else {
              _tmpAckChecksum = _cursor.getBlob(_cursorIndexOfAckChecksum);
            }
            final String _tmpDeliveryStatus;
            _tmpDeliveryStatus = _cursor.getString(_cursorIndexOfDeliveryStatus);
            final int _tmpHeardByCount;
            _tmpHeardByCount = _cursor.getInt(_cursorIndexOfHeardByCount);
            final int _tmpAttempt;
            _tmpAttempt = _cursor.getInt(_cursorIndexOfAttempt);
            final boolean _tmpIsSentByMe;
            final int _tmp_1;
            _tmp_1 = _cursor.getInt(_cursorIndexOfIsSentByMe);
            _tmpIsSentByMe = _tmp_1 != 0;
            _item = new MessageEntity(_tmpId,_tmpSenderId,_tmpSenderName,_tmpChannelHash,_tmpContent,_tmpTimestamp,_tmpIsPrivate,_tmpAckChecksum,_tmpDeliveryStatus,_tmpHeardByCount,_tmpAttempt,_tmpIsSentByMe);
            _result.add(_item);
          }
          return _result;
        } finally {
          _cursor.close();
        }
      }

      @Override
      protected void finalize() {
        _statement.release();
      }
    });
  }

  @Override
  public Flow<List<MessageEntity>> getAllMessages() {
    final String _sql = "SELECT * FROM messages ORDER BY timestamp ASC";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 0);
    return CoroutinesRoom.createFlow(__db, false, new String[] {"messages"}, new Callable<List<MessageEntity>>() {
      @Override
      @NonNull
      public List<MessageEntity> call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfId = CursorUtil.getColumnIndexOrThrow(_cursor, "id");
          final int _cursorIndexOfSenderId = CursorUtil.getColumnIndexOrThrow(_cursor, "senderId");
          final int _cursorIndexOfSenderName = CursorUtil.getColumnIndexOrThrow(_cursor, "senderName");
          final int _cursorIndexOfChannelHash = CursorUtil.getColumnIndexOrThrow(_cursor, "channelHash");
          final int _cursorIndexOfContent = CursorUtil.getColumnIndexOrThrow(_cursor, "content");
          final int _cursorIndexOfTimestamp = CursorUtil.getColumnIndexOrThrow(_cursor, "timestamp");
          final int _cursorIndexOfIsPrivate = CursorUtil.getColumnIndexOrThrow(_cursor, "isPrivate");
          final int _cursorIndexOfAckChecksum = CursorUtil.getColumnIndexOrThrow(_cursor, "ackChecksum");
          final int _cursorIndexOfDeliveryStatus = CursorUtil.getColumnIndexOrThrow(_cursor, "deliveryStatus");
          final int _cursorIndexOfHeardByCount = CursorUtil.getColumnIndexOrThrow(_cursor, "heardByCount");
          final int _cursorIndexOfAttempt = CursorUtil.getColumnIndexOrThrow(_cursor, "attempt");
          final int _cursorIndexOfIsSentByMe = CursorUtil.getColumnIndexOrThrow(_cursor, "isSentByMe");
          final List<MessageEntity> _result = new ArrayList<MessageEntity>(_cursor.getCount());
          while (_cursor.moveToNext()) {
            final MessageEntity _item;
            final String _tmpId;
            _tmpId = _cursor.getString(_cursorIndexOfId);
            final byte[] _tmpSenderId;
            _tmpSenderId = _cursor.getBlob(_cursorIndexOfSenderId);
            final String _tmpSenderName;
            if (_cursor.isNull(_cursorIndexOfSenderName)) {
              _tmpSenderName = null;
            } else {
              _tmpSenderName = _cursor.getString(_cursorIndexOfSenderName);
            }
            final byte _tmpChannelHash;
            _tmpChannelHash = (byte) (_cursor.getShort(_cursorIndexOfChannelHash));
            final String _tmpContent;
            _tmpContent = _cursor.getString(_cursorIndexOfContent);
            final long _tmpTimestamp;
            _tmpTimestamp = _cursor.getLong(_cursorIndexOfTimestamp);
            final boolean _tmpIsPrivate;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsPrivate);
            _tmpIsPrivate = _tmp != 0;
            final byte[] _tmpAckChecksum;
            if (_cursor.isNull(_cursorIndexOfAckChecksum)) {
              _tmpAckChecksum = null;
            } else {
              _tmpAckChecksum = _cursor.getBlob(_cursorIndexOfAckChecksum);
            }
            final String _tmpDeliveryStatus;
            _tmpDeliveryStatus = _cursor.getString(_cursorIndexOfDeliveryStatus);
            final int _tmpHeardByCount;
            _tmpHeardByCount = _cursor.getInt(_cursorIndexOfHeardByCount);
            final int _tmpAttempt;
            _tmpAttempt = _cursor.getInt(_cursorIndexOfAttempt);
            final boolean _tmpIsSentByMe;
            final int _tmp_1;
            _tmp_1 = _cursor.getInt(_cursorIndexOfIsSentByMe);
            _tmpIsSentByMe = _tmp_1 != 0;
            _item = new MessageEntity(_tmpId,_tmpSenderId,_tmpSenderName,_tmpChannelHash,_tmpContent,_tmpTimestamp,_tmpIsPrivate,_tmpAckChecksum,_tmpDeliveryStatus,_tmpHeardByCount,_tmpAttempt,_tmpIsSentByMe);
            _result.add(_item);
          }
          return _result;
        } finally {
          _cursor.close();
        }
      }

      @Override
      protected void finalize() {
        _statement.release();
      }
    });
  }

  @Override
  public Object getMessageById(final String messageId,
      final Continuation<? super MessageEntity> $completion) {
    final String _sql = "SELECT * FROM messages WHERE id = ?";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 1);
    int _argIndex = 1;
    _statement.bindString(_argIndex, messageId);
    final CancellationSignal _cancellationSignal = DBUtil.createCancellationSignal();
    return CoroutinesRoom.execute(__db, false, _cancellationSignal, new Callable<MessageEntity>() {
      @Override
      @Nullable
      public MessageEntity call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfId = CursorUtil.getColumnIndexOrThrow(_cursor, "id");
          final int _cursorIndexOfSenderId = CursorUtil.getColumnIndexOrThrow(_cursor, "senderId");
          final int _cursorIndexOfSenderName = CursorUtil.getColumnIndexOrThrow(_cursor, "senderName");
          final int _cursorIndexOfChannelHash = CursorUtil.getColumnIndexOrThrow(_cursor, "channelHash");
          final int _cursorIndexOfContent = CursorUtil.getColumnIndexOrThrow(_cursor, "content");
          final int _cursorIndexOfTimestamp = CursorUtil.getColumnIndexOrThrow(_cursor, "timestamp");
          final int _cursorIndexOfIsPrivate = CursorUtil.getColumnIndexOrThrow(_cursor, "isPrivate");
          final int _cursorIndexOfAckChecksum = CursorUtil.getColumnIndexOrThrow(_cursor, "ackChecksum");
          final int _cursorIndexOfDeliveryStatus = CursorUtil.getColumnIndexOrThrow(_cursor, "deliveryStatus");
          final int _cursorIndexOfHeardByCount = CursorUtil.getColumnIndexOrThrow(_cursor, "heardByCount");
          final int _cursorIndexOfAttempt = CursorUtil.getColumnIndexOrThrow(_cursor, "attempt");
          final int _cursorIndexOfIsSentByMe = CursorUtil.getColumnIndexOrThrow(_cursor, "isSentByMe");
          final MessageEntity _result;
          if (_cursor.moveToFirst()) {
            final String _tmpId;
            _tmpId = _cursor.getString(_cursorIndexOfId);
            final byte[] _tmpSenderId;
            _tmpSenderId = _cursor.getBlob(_cursorIndexOfSenderId);
            final String _tmpSenderName;
            if (_cursor.isNull(_cursorIndexOfSenderName)) {
              _tmpSenderName = null;
            } else {
              _tmpSenderName = _cursor.getString(_cursorIndexOfSenderName);
            }
            final byte _tmpChannelHash;
            _tmpChannelHash = (byte) (_cursor.getShort(_cursorIndexOfChannelHash));
            final String _tmpContent;
            _tmpContent = _cursor.getString(_cursorIndexOfContent);
            final long _tmpTimestamp;
            _tmpTimestamp = _cursor.getLong(_cursorIndexOfTimestamp);
            final boolean _tmpIsPrivate;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsPrivate);
            _tmpIsPrivate = _tmp != 0;
            final byte[] _tmpAckChecksum;
            if (_cursor.isNull(_cursorIndexOfAckChecksum)) {
              _tmpAckChecksum = null;
            } else {
              _tmpAckChecksum = _cursor.getBlob(_cursorIndexOfAckChecksum);
            }
            final String _tmpDeliveryStatus;
            _tmpDeliveryStatus = _cursor.getString(_cursorIndexOfDeliveryStatus);
            final int _tmpHeardByCount;
            _tmpHeardByCount = _cursor.getInt(_cursorIndexOfHeardByCount);
            final int _tmpAttempt;
            _tmpAttempt = _cursor.getInt(_cursorIndexOfAttempt);
            final boolean _tmpIsSentByMe;
            final int _tmp_1;
            _tmp_1 = _cursor.getInt(_cursorIndexOfIsSentByMe);
            _tmpIsSentByMe = _tmp_1 != 0;
            _result = new MessageEntity(_tmpId,_tmpSenderId,_tmpSenderName,_tmpChannelHash,_tmpContent,_tmpTimestamp,_tmpIsPrivate,_tmpAckChecksum,_tmpDeliveryStatus,_tmpHeardByCount,_tmpAttempt,_tmpIsSentByMe);
          } else {
            _result = null;
          }
          return _result;
        } finally {
          _cursor.close();
          _statement.release();
        }
      }
    }, $completion);
  }

  @NonNull
  public static List<Class<?>> getRequiredConverters() {
    return Collections.emptyList();
  }
}
