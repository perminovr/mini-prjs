
#ifndef GOOSESUB_H_
#define GOOSESUB_H_

#include <string>
#include "GooseFrame.hpp"



/*!
 * @fn MAC_to_string
 * 
 * @brief: Конвертация массива 6 байт MAC адреса в строку
 * 
 * @param MAC - массив 6 байт MAC адреса
 * @param splitter - разделитель октет
*/
extern std::string MAC_to_string(const uint8_t *MAC, char splitter);
/*!
 * @fn string_to_MAC
 * 
 * @brief: Конвертация строки с MAC адресом в массив 6 байт
 * 
 * @param MAC - строка с MAC адресом
 * @param splitter - разделитель октет
 * @param data - массив 6 байт MAC адреса
*/
extern void string_to_MAC(const std::string &MAC, char splitter, uint8_t *data);



/*!
 * @class GooseSub
 * 
 * @brief: Подписчик goose-рассылок. Обработка кадра данных согласно заданным фильтрам
*/
class GooseSub {
public:

    /*!
     * @struct GooseSub::Filter
     * 
     * @brief: Фильтры кадров данных для goose-рассылок
    */
    struct Filter {
        // для пользователя, не обрабатывается, отправляется в callback
        uint32_t id; 
        // пользовательские данные
        void *priv;
        // callback на прием кадра // todo emit?
        void (*ReceiveCallback)(const GooseFrame &frame, uint32_t id, void *priv);

        // фильтр адреса назначения: сетевой порядок байт
		union {
			// не задано / броадкаст / юникаст
			uint8_t destMAC[ETH_ALEN];
			struct {
				// первый адрес мультикастовой группы
				uint8_t destMAC_multStart[ETH_ALEN];
				// последний адрес мультикастовой группы
				uint8_t destMAC_multEnd[ETH_ALEN];
			};
		};
        // фильтр адреса источника: сетевой порядок байт
		union {
			// не задано / броадкаст / юникаст
			uint8_t sourceMAC[ETH_ALEN];
			struct {
				// первый адрес мультикастовой группы
				uint8_t sourceMAC_multStart[ETH_ALEN];
				// последний адрес мультикастовой группы
				uint8_t sourceMAC_multEnd[ETH_ALEN];
			};
		};
        // фильтр по vlan (не поддержан)
        uint16_t vlanID;
        // фильтр по appid
        uint16_t appID;
        // фильтр по goCBRef 
        std::string goCBRef;
        // фильтр по datSet 
        std::string datSet;
        // фильтр по goID 
        std::string goID;
        // специальные фильтры (флаги)
        union {
            struct {
                // вызывать callback только по обновлению данных кадра
                uint32_t callOnce : 1;
                // вызывать все callback для кадра, удовлетворяющего нескольким фильтрам
                // по умолчанию вызывается только callback первого установленного фильтра (==0) 
                uint32_t notUnique : 1;
            };
            uint32_t _fbits;
        };
    };

    /*!
     * @fn GooseSub::GooseSub
     * 
     * @brief: конструктор
     * 
     * @param devName - имя интрефейса, например 'eth0'
    */
    GooseSub(const std::string &devName);
    /*!
     * @fn GooseSub::~GooseSub
     * 
   * @brief: деструктор
    */
    ~GooseSub();

    /*!
     * @fn GooseSub::Subcribe
     * 
     * @brief: подписка на goose-рассылку согласно заданному фильтру
     * 
     * @param filter - фильтр кадров
    */
    void Subcribe(const GooseSub::Filter &filter);
    /*!
     * @fn GooseSub::Unsubscribe
     * 
     * @brief: отписка от goose-рассылки с заданным фильтром
     * 
     * @param filter - фильтр кадров
    */
    void Unsubscribe(const GooseSub::Filter &filter);

    /*!
     * @fn GooseSub::SetupLogger
     * 
     * @brief: установка логгера
    */
    void SetupLogger(const DrvLogger *logger);

private:
    /*!
     * @property GooseSub::priv
     * 
     * @brief: private часть драйвера GooseSub
    */
    void *priv;
    /*!
     * @fn GooseSub::MainLoop
     * 
     * @brief: основной цикл работы драйвера
    */
    void MainLoop(void);
};



#endif /* GOOSESUB_H_ */
