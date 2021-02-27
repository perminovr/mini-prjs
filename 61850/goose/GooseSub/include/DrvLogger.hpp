
#ifndef DRVLOGGER_H_
#define DRVLOGGER_H_

#include <cstdio>



/*!
 * @class DrvLogger
 * 
 * @brief: Логгер
*/
class DrvLogger {
public:
    /*!
     * @enum GooseSub::LogLevel
     * 
     * @brief: Уровень логгирования
    */
    enum LogLevel {
        No,         // без сообщений
        Error,      // ошибки
        Warning,    // предупреждения
        Note,       // замечания
        Trace,      // все сообщения
        Dump        // дампы буферов
    };

    /*!
     * @fn DrvLogger::DrvLogger
     * 
     * @brief: конструктор
     * 
     * @param fd - файловый дескриптор
     * @param lvl - уровень логгирования (по умолчанию = LogLevel::No)
    */
    DrvLogger(int fd);
    DrvLogger(int fd, LogLevel lvl);

    /*!
     * @fn DrvLogger::~DrvLogger
     * 
     * @brief: деструктор
    */
    ~DrvLogger() = default;

    /*!
     * @fn DrvLogger::Print
     * 
     * @brief: печать лога
     * 
     * @param format - формат выводимой строки @ref std printf
     * @param args - аргументы форматированной строки
    */
    void Print(DrvLogger::LogLevel lvl, const char *format, ...) const;

    /*!
     * @fn DrvLogger::DumpData
     * 
     * @brief: печать дампа
    */
    void DumpData(const void* data, size_t size) const;

    /*!
     * @fn DrvLogger::SetupDebugLvl
     * 
     * @brief: Установка уровня лога
    */
    void SetupDebugLvl(LogLevel lvl);

private:
    /*!
     * @property DrvLogger::fd
     * 
     * @brief: файловый дескриптор для ведения лога
    */
    int fd;
    /*!
     * @property DrvLogger::lvl
     * 
     * @brief: уровень логгирования
    */
    DrvLogger::LogLevel lvl;
};

    

#endif /* DRVLOGGER_H_ */