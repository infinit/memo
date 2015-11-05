$(document).ready(function() {

  var users = $('.estimation table .users td span');
  var usersLabel = $('#usersLabel span');
  var capacities = $('.estimation table tr th');
  var capacityLabel = $('#capacityLabel span');
  var prices = $('.estimation table .price td span');

  // Price Update
  function updatePrice() {
    $.each(prices, function(key, element) {
      var service = $(element).parent().attr('data-service');
      var el = $(users).parent().filter("[data-service='" + service + "']");
      var current_users = $(usersLabel).text();
      var price = $(el).find('span').text();
      var new_price;

      new_price = $(element).attr('data-price') * 12 * current_users;

      if (service === 'infinit') {
        new_price = new_price + ($(capacityLabel).text() * 0.03 * 2);
      }

      $(element).text(new_price);
    });
  }

  // Users Slider
  $("#usersInput").bind("slider:changed", function (event, data) {
    usersLabel.text(data.value);
    updatePrice();
  });

  // Capacity Slider
  $("#capacityInput").bind("slider:changed", function (event, data) {

    $.each(capacities, function(key, element) {
      var service = $(element).attr('data-service');

      if ($(element).attr('data-max')) {
        if ($(element).attr('data-max') < data.value) {
          $("[data-service='" + service + "']").addClass('disabled');
        } else {
          $("[data-service='" + service + "']").removeClass('disabled');
        }
      }
    });

    capacityLabel.text(data.value);
    updatePrice();
  });

});